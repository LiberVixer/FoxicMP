"""Force real producer/decoder observations to straddle heartbeat expiry.

The source response is captured before the deadline and held at the client;
no player/worker code or clock is changed. A paused decoder may have observed
expiry by the next RPC, while the captured producer response is still healthy.
"""
import json
import time

from progressive_continuous import AAC_MODE
import progressive_continuous_heartbeat_conformance as heartbeat
from progressive_player_crash_recovery import sha


def exercise(peers, root, report, mode=AAC_MODE, helper=None):
    assert mode == AAC_MODE
    subject = helper or heartbeat
    original = peers[0].client.request
    last_heartbeat, injected = None, []

    def delayed(message, timeout=5):
        nonlocal last_heartbeat
        reply = original(message, timeout=timeout)
        if message['action'] == 'continuousRemuxHeartbeat':
            last_heartbeat = time.monotonic()
        if (message['action'] == 'continuousRemuxStatus' and last_heartbeat is not None
                and not injected and not reply['errorCode']
                and 29.5 <= time.monotonic()-last_heartbeat < 30):
            began = time.monotonic()
            time.sleep(max(0, last_heartbeat+30.25-time.monotonic()))
            injected.append(dict(heldHealthyProducerReply=reply, afterHeartbeatSeconds=began-last_heartbeat,
                                 holdSeconds=time.monotonic()-began))
            (report/'injection.json').write_text(json.dumps(injected, indent=2)+'\n', encoding='utf-8')
        return reply

    peers[0].client.request = delayed
    try:
        result = subject.exercise(peers, root, report, profiles=('paused',), completed=False, media_mode=mode)
        assert len(injected) == 1 and injected[0]['holdSeconds'] < 2, injected
        result.update(clientObservationDelay=True, helperSourceSha256=sha(subject.__file__), injection=injected,
                      playerOrWorkerHooks=False, clocksNotChanged=True)
        return result
    finally:
        peers[0].client.request = original
