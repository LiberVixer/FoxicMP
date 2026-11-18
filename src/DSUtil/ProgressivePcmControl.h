// Private DirectShow PCM window handshake, GPL-3.0-or-later.
#pragma once
interface __declspec(uuid("89A03A9A-C892-4B8E-95B4-612349981ACE"))
IProgressivePcmControl : public IUnknown {
    // Version 1: MP3 raw window. Version 2: explicitly admitted continuous
    // Opus, 48kHz/960-sample packets, raw pre-skip=312; no AAC widening.
    // Called after NewSegment, before the first compressed packet. The first
    // packet's discontinuity flush must run before the decoder is armed.
    STDMETHOD(ArmPcmWindow)(UINT version, ULONGLONG begin, ULONGLONG end,
                           UINT rate, ULONGLONG decodeFirst) PURE;
    // Bit 1: armed, bit 2: applied to this segment, bit 4: failed.
    STDMETHOD(PcmWindowState)(DWORD* flags) PURE;
};
// Independent IID keeps the existing MP3 ABI/version-one admission unchanged.
interface __declspec(uuid("620E4C65-7403-41C0-BA86-440A9A559B3E"))
IProgressiveAacPcmControl : public IUnknown {
    STDMETHOD(ArmAacPcmWindow)(UINT version, UINT rate, UINT channels,
        ULONGLONG samples, UINT initialSkip, ULONGLONG target, ULONGLONG decodeFirst) PURE;
    STDMETHOD(AacPcmWindowState)(DWORD* flags) PURE;
};
// Separate explicit ordinary-M4A admission; AAC-LC v1 remains unchanged.
interface __declspec(uuid("FC3A6518-96A5-4B63-B7C2-96BA208697E1"))
IProgressiveM4AAacControl : public IUnknown {
    STDMETHOD(ArmM4AAac)(UINT rate,UINT channels,ULONGLONG samples,
        UINT initialSkip,UINT frameSamples,ULONGLONG target,ULONGLONG decodeFirst) PURE;
};
// Separate IID: provisional bounded PCM stop may advance before each packet.
// Fixed MP3 v1 / Opus v2 / AAC admissions retain their exact immutable stop.
interface __declspec(uuid("55679A44-A59A-48B0-A4D1-E58B70D5CCAF"))
IProgressiveGrowingOpusControl : public IUnknown {
    STDMETHOD(ArmGrowingOpus)(ULONGLONG begin, ULONGLONG end, ULONGLONG decodeFirst) PURE;
    STDMETHOD(AdvanceGrowingOpus)(ULONGLONG end) PURE;
};
// Packet-derived sample offsets; existing fixed-960 Opus IIDs stay unchanged.
interface __declspec(uuid("5EA8EFC4-964D-4C80-AF8E-69B9807B1CC2"))
IProgressivePacketOpusControl : public IUnknown {
    STDMETHOD(ArmPacketOpus)(UINT preSkip,ULONGLONG begin,ULONGLONG end,ULONGLONG decodeFirst) PURE;
    STDMETHOD(AdvancePacketOpus)(ULONGLONG end) PURE;
};
