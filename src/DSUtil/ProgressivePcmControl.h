// Private DirectShow PCM window handshake, GPL-3.0-or-later.
#pragma once
interface __declspec(uuid("89A03A9A-C892-4B8E-95B4-612349981ACE"))
IProgressivePcmControl : public IUnknown {
    // Called after NewSegment, before the first compressed packet. The first
    // packet's discontinuity flush must run before the decoder is armed.
    STDMETHOD(ArmPcmWindow)(UINT version, ULONGLONG begin, ULONGLONG end,
                           UINT rate, ULONGLONG decodeFirst) PURE;
    // Bit 1: armed, bit 2: applied to this segment, bit 4: failed.
    STDMETHOD(PcmWindowState)(DWORD* flags) PURE;
};
