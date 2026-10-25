// FoxicMP reader lifecycle model. GPL-3.0-or-later.
#pragma once
#include "progressivesession.h"
namespace foxicmp { namespace progressive {
struct ProcessIdentity {
    uint32_t pid=0;
    // Trusted OS adapter supplies creation time plus boot identity where needed.
    // This token must not be accepted from an unverified IPC request.
    std::string birth;
    bool operator==(const ProcessIdentity& b) const {return pid==b.pid && birth==b.birth;}
};
enum class ReaderState { Attached, Closing, Released };
enum class ProcessObservation { Unknown, Same, Exited, ReusedPid };
struct Reader {
    std::string id,session;uint64_t generation=0;ProcessIdentity process;
    std::set<std::string> resources;ReaderState state=ReaderState::Attached;
};
class ReaderBook {
    // The backend serializes calls. This class performs no I/O and owns no timer.
    std::map<std::string,Reader> items;
    Reader& owned(const std::string& id,const ProcessIdentity& peer) {
        auto found=items.find(id);Require(found!=items.end(),"readerId","Unknown reader","unknown_reader");
        Require(found->second.process==peer,"readerId","Reader belongs to another process incarnation","reader_owner_mismatch");return found->second;
    }
public:
    void attach(const Snapshot& snapshot,const std::string& id,const ProcessIdentity& peer,const std::set<std::string>& resources) {
        Require(id.size()==32 && id.find_first_not_of("0123456789abcdef")==std::string::npos,"readerId","Invalid reader ID");
        Require(peer.pid && !peer.birth.empty() && peer.birth.size()<=128,"process","Missing authenticated process creation identity");
        Require(!resources.empty(),"resources","Reader needs a resource scope");
        for(const auto& resource:resources)Require(snapshot.resources.count(resource)!=0,"resources","Unknown reader resource");
        auto existing=items.find(id);
        if(existing!=items.end()) {
            const auto& r=existing->second;
            Require(r.process==peer && r.session==snapshot.id && r.generation==snapshot.generation && r.resources==resources && r.state==ReaderState::Attached,"readerId","Conflicting or already closed reader","reader_conflict");return;
        }
        for(const auto& resource:resources)Require(!snapshot.resources.at(resource).retired,"resources","Resource left the live window","resource_retired");
        Require(items.size()<MaxReaders,"readers","Reader limit reached","reader_limit");
        items.emplace(id,Reader{id,snapshot.id,snapshot.generation,peer,resources,ReaderState::Attached});
    }
    void beginRelease(const std::string& id,const ProcessIdentity& peer) {
        auto& r=owned(id,peer);if(r.state!=ReaderState::Released)r.state=ReaderState::Closing;
    }
    void filesClosed(const std::string& id,const ProcessIdentity& peer) {
        auto& r=owned(id,peer);
        Require(r.state!=ReaderState::Attached,"readerId","Begin reader close before confirming file release","reader_not_closing");r.state=ReaderState::Released;
    }
    void observe(const std::string& id,ProcessObservation observation) {
        auto found=items.find(id);Require(found!=items.end(),"readerId","Unknown reader","unknown_reader");
        // Only an OS-authenticated observation can call this method. No timeouts,
        // missing heartbeats, disconnected clients or permission errors count.
        if(observation==ProcessObservation::Exited || observation==ProcessObservation::ReusedPid)
            found->second.state=ReaderState::Released;
    }
    bool inUse(const std::string& session,uint64_t generation,const std::string& resource) const {
        for(const auto& item:items) {const auto& r=item.second;
            if(r.session==session && r.generation==generation && r.resources.count(resource) && r.state!=ReaderState::Released)return true;
        }return false;
    }
    ReaderState state(const std::string& id) const {
        auto found=items.find(id);Require(found!=items.end(),"readerId","Unknown reader","unknown_reader");return found->second.state;
    }
    void forgetReleased(const std::string& id) {
        auto found=items.find(id);Require(found!=items.end() && found->second.state==ReaderState::Released,"readerId","Cannot forget a live reader","reader_in_use");items.erase(found);
    }
};
}} // namespace foxicmp::progressive
