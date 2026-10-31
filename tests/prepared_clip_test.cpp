// Bounded immutable decoder input and protocol validation. GPL-3.0-or-later.
#include "../shared/preparedclip.h"
#include <iostream>
#include <thread>
using namespace foxicmp::progressive;
static void check(bool value) { if (!value) throw std::runtime_error("Prepared clip invariant failed"); }
template<class F> static void rejected(F test) {
    bool failed = false;
    try { test(); } catch (const Error&) { failed = true; }
    check(failed);
}
int main() {
    try {
        auto data = std::make_shared<const std::vector<char>>(2097152, 'a');
        auto clip = PreparedClips::create(data);
        std::vector<char> output(data->size());
        check(clip->read(0, output.data(), output.size()) == 1048576);
        check(clip->read(data->size()-1, output.data(), 2) == 1);
        check(output[0] == 'a');
        check(clip->read(data->size(), output.data(), 1) == 0);
        check(clip->read(data->size()+1, output.data(), 1) == -1);
        check(clip->read(0, nullptr, 1) == -1);
        auto uri = clip->uri();
        check(PreparedClips::find(uri) == clip);
        check(!PreparedClips::find(uri+"/../other"));
        check(!PreparedClips::find("file:///tmp/input.mp4"));
        data.reset(); // Decoder input survives preparation ownership release.
        std::thread reader([&] { for (unsigned i=0;i<1000;++i) check(clip->read(0,output.data(),100)==100); });
        reader.join();
        clip->cancelled = true;
        check(clip->read(0, output.data(), 1) == -1);
        check(!PreparedClips::find(uri));
        clip.reset();
        check(!PreparedClips::find(uri));
        rejected([] { PreparedClips::create(nullptr); });
        rejected([] { PreparedClips::create(std::make_shared<const std::vector<char>>()); });
        rejected([] { PreparedClips::create(std::make_shared<const std::vector<char>>(16777217)); });
        std::vector<std::shared_ptr<PreparedClip>> retained;
        auto small = std::make_shared<const std::vector<char>>(1);
        for (unsigned i=0;i<16;++i) retained.push_back(PreparedClips::create(small));
        rejected([&] { PreparedClips::create(small); });
        retained.clear();
        check(bool(PreparedClips::create(small)));
        const std::string start = R"({"version":1,"action":"previewRemux","probeId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","requestId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
        auto command = ParsePreviewCommand(start);
        check(!command.replace && !command.activate && command.id==std::string(32,'a'));
        rejected([] { ParsePreviewCommand(R"({"version":1,"action":"previewRemux","probeId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"); });
        rejected([] { ParsePreviewCommand(R"({"version":1,"action":"previewStatus","requestId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","previewId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"); });
        rejected([] { ParsePreviewCommand(R"({"version":1,"action":"previewStatus","requestId":"../../etc/passwd"})"); });
        rejected([] { ParsePreviewCommand(R"({"version":1,"action":"previewControl","previewId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","command":"seek","seconds":-1})"); });
        rejected([] { ParsePreviewCommand(R"({"version":1,"action":"previewCancel","previewId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","path":"input.mp4"})"); });
        std::cout << "Prepared clip bounds, lifetime, limits, cancellation and protocol: PASS\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
