#pragma once
// Platform-neutral defaults shared by Classic and both Qt editions.
// Apply only as defaults: saved preferences always take precedence.
namespace FoxicMPFamily {
inline constexpr int Volume = 75;
inline constexpr int VolumeStep = 5;
inline constexpr bool Mute = false;
inline constexpr bool RememberPosition = true;
inline constexpr bool RememberGeometry = true;
inline constexpr bool KeepWindowVideoAspect = false;
inline constexpr bool Osd = false;
inline constexpr int SeekSmallMs = 1000;
inline constexpr int SeekMediumMs = 5000;
inline constexpr int SeekLargeMs = 20000;
} // namespace FoxicMPFamily
