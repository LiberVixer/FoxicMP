// Local, bounded page I/O for the internal segmented catalog model.
// Used by the separately negotiated catalog playback path. GPL-3.0-or-later.
#pragma once
#include "progressivesegmentcatalog.h"
#include "progressiveresourceio.h"

namespace foxicmp { namespace progressive {
class LocalSegmentCatalog {
    resource_detail::Directory directory;
    std::unique_ptr<SegmentCatalog> current;
    std::map<std::string,resource_detail::Info> bindings;
    std::atomic<bool> cancelled{false};
    void checkCancelled() const {Require(!cancelled.load(),"segmentCatalog","Catalog reader was cancelled","cancelled");}
    std::pair<std::string,resource_detail::Info> read(const SegmentCatalogPage& page) const {
        checkCancelled();directory.verify();
        auto file=[&]() {
            try{return directory.open(page.name);}
            catch(const Error& error) {
                if(error.code=="waiting-resource") {
                    if(bindings.count(page.id))throw Error("missing-index","segmentCatalog.page","Previously published catalog page is missing");
                    throw Error("waiting-index","segmentCatalog.page","Catalog page has not appeared yet");
                }
                throw;
            }
        }();
        checkCancelled();const auto before=resource_detail::Inspect(file);
        const auto previous=bindings.find(page.id);
        Require(previous==bindings.end() || (previous->second.device==before.device && previous->second.fileId==before.fileId),
            "segmentCatalog.page","Previously published catalog page was replaced","source-replaced");
        // Check the physical size BEFORE allocation or I/O. Never read beyond
        // the already bounded descriptor, including a speculative extra byte.
        Require(before.size==page.length,"segmentCatalog.page","Catalog page length mismatch","catalog_length_mismatch");
        std::string text(page.length,'\0');
        for(size_t at=0;at<text.size();) {
            checkCancelled();const auto count=(std::min)(size_t(4096),text.size()-at);
            const auto received=resource_detail::ReadAt(file,at,&text[at],count,cancelled);
            Require(received==count,"segmentCatalog.page","Catalog page was truncated during reading","source-truncated");at+=received;
        }
        checkCancelled();const auto after=resource_detail::Inspect(file);
        Require(before.device==after.device && before.fileId==after.fileId,"segmentCatalog.page","Catalog page identity changed","source-replaced");
        Require(after.size==page.length,"segmentCatalog.page","Catalog page size changed during reading","catalog_length_mismatch");
        directory.verify();return {std::move(text),after};
        // file is closed here, before the decoder consumes the returned bytes.
    }
public:
    struct Prepared {
        std::unique_ptr<SegmentCatalog> value;
        std::map<std::string,resource_detail::Info> bindings;
    };
    // publish/catalog are serialized by the owner on its worker. Only cancel
    // is concurrent. No UI dispatch, timer, network, media handles or leases.
    explicit LocalSegmentCatalog(const std::filesystem::path& root):directory(root){}
    void cancel(){cancelled.store(true);}
    Prepared prepare(const std::string& json) const {
        checkCancelled();std::map<std::string,resource_detail::Info> nextBindings;
        auto next=DecodeSegmentCatalog(json,[&](const SegmentCatalogPage& page) {
            auto value=read(page);
            for(const auto& bound:nextBindings)
                Require(bound.second.device!=value.second.device || bound.second.fileId!=value.second.fileId,
                    "segmentCatalog.page","Different catalog pages refer to the same file","resource-alias");
            nextBindings.emplace(page.id,value.second);return std::move(value.first);
        });
        checkCancelled();if(current)AdvanceSegmentCatalog(*current,next);
        // All page handles have closed; failed decode/update never commits new
        // bindings or overwrites the last accepted model. Allocate before swap.
        auto accepted=std::make_unique<SegmentCatalog>(std::move(next));checkCancelled();
        return {std::move(accepted),std::move(nextBindings)};
    }
    // The serialized owner commits only its own most recently prepared value.
    // No filesystem I/O or allocation after the media publication succeeds.
    void commit(Prepared&& accepted) noexcept {
        current.swap(accepted.value);bindings.swap(accepted.bindings);
    }
    void publish(const std::string& json) {
        auto accepted=prepare(json);commit(std::move(accepted));
    }
    const SegmentCatalog& catalog() const {
        Require(bool(current),"segmentCatalog","No accepted catalog","waiting-index");return *current;
    }
};
}} // namespace foxicmp::progressive
