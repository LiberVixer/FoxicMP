// Actual receipt API + Qt QTRY observation; no media backend or user profile.
#include "../../shared/progressivereaderlease.h"
#include <QtCore>
#include <QtTest>
#include <qscopeguard.h>
namespace p=foxicmp::progressive;
class ReceiptObservationTest:public QObject {
    Q_OBJECT
private slots:
    void receipt_data(){
        QTest::addColumn<bool>("contended");
        QTest::newRow("ordinary")<<false;
        QTest::newRow("busy-after-proof")<<true;
    }
    void receipt(){
        QFETCH(bool,contended);
        QTemporaryDir temporary;QVERIFY(temporary.isValid());
        const auto root=std::filesystem::u8path(temporary.path().toStdString());
        const auto mediaPath=root/"input.part",registry=root/"registry";
        QFile file(QString::fromStdString(mediaPath.u8string()));
        QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("media",5),qint64(5));file.close();
        uint64_t device=0,fileId=0;
#ifdef Q_OS_WIN
        p::lease_detail::Handle media(CreateFileW(mediaPath.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        QVERIFY(media.value!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(media.value,&info));
        device=info.dwVolumeSerialNumber;fileId=(uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
#else
        p::lease_detail::Handle media(::open(mediaPath.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
        struct stat info{};QVERIFY(media.value>=0 && !fstat(media.value,&info));
        device=uint64_t(info.st_dev);fileId=uint64_t(info.st_ino);
#endif
        auto lease=p::ReaderLease::Create(registry,p::lease_detail::RandomId(),1,{{"media",device,fileId}});
        const auto id=QString::fromStdString(lease->id());
#ifdef Q_OS_WIN
        QVERIFY(CloseHandle(media.value));media.value=INVALID_HANDLE_VALUE;
#else
        QCOMPARE(::close(media.value),0);media.value=-1;
#endif
        lease->filesClosed();lease.reset();
        const auto request=[&](const QString& action){
            const QJsonObject input{{"version",1},{"action",action},{"readerId",id}};
            return QJsonDocument::fromJson(QByteArray::fromStdString(p::ReaderRequest(registry,QJsonDocument(input).toJson(QJsonDocument::Compact).toStdString()))).object();
        };
        const auto released=[](const QJsonObject& reply){return reply["ok"].toBool() && reply["reader"].toObject()["released"].toBool();};
        p::lease_detail::Directory receipts(registry,false);
        auto guard=receipts.file(".guard",true);
        bool guardHeld=false;int calls=0;QJsonObject firstProof,lastReply;
        const bool legacy=qEnvironmentVariableIntValue("FOXICMP_TEST_LEGACY_RECEIPT_ORACLE")==1;
        QElapsedTimer elapsed;elapsed.start();
        const auto log=qScopeGuard([&]{
            const QJsonObject row{{"qtVersion",qVersion()},{"legacy",legacy},{"contended",contended},{"calls",calls},
                {"milliseconds",double(elapsed.elapsed())},{"guardHeld",guardHeld},{"firstProof",firstProof},{"lastReply",lastReply}};
            qInfo().noquote()<<"RECEIPT_ORACLE"<<QJsonDocument(row).toJson(QJsonDocument::Compact);
        });
        const auto poll=[&]{
            ++calls;lastReply=request("readerStatus");const bool positive=released(lastReply);
            if(positive && firstProof.isEmpty()){
                firstProof=lastReply;
                // Acquire only after observing a real positive receipt. Future
                // metadata requests may be busy; the proof remains valid.
                if(contended)guardHeld=guard.lock();
            }
            return positive;
        };
        bool confirmed=false;
        const auto latched=[&]{return confirmed || (confirmed=poll());};
        // A lambda confines QTRY's return on the intentional legacy FAIL, so
        // registry cleanup below still runs and the first error is retained.
        const auto verify=[&]{
            if(legacy){QTRY_VERIFY_WITH_TIMEOUT(poll(),5000);}
            else {QTRY_VERIFY_WITH_TIMEOUT(latched(),5000);}
        };
        verify();
        QVERIFY(released(firstProof));
        const auto proof=firstProof["reader"].toObject();QVERIFY(proof["filesClosed"].toBool());
        QCOMPARE(proof["reason"].toString(),QString("files-closed"));
        if(contended){
            QVERIFY(guardHeld);
            QCOMPARE(request("readerStatus")["code"].toString(),QString("reader_registry_busy"));
            QCOMPARE(request("readerRelease")["code"].toString(),QString("reader_registry_busy"));
        }
        if(!legacy)QCOMPARE(calls,1);
        // Drop the guard before acknowledgement. Moving its native owner closes
        // the old handle when the short scope ends on both operating systems.
        {auto held=std::move(guard);}
        QVERIFY(released(request("readerStatus")));
        QVERIFY(request("readerForget")["forgotten"].toBool());
    }
};
QTEST_GUILESS_MAIN(ReceiptObservationTest)
#include "receipt_observation_test.moc"
