#ifndef WAV_READER_HPP_INCLUDED
#define WAV_READER_HPP_INCLUDED

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DistrhoUtils.hpp"

START_NAMESPACE_DISTRHO

// Paste the body of readWav() here exactly as it is in ExamplePlugin.cpp,
// changing only "static bool readWav(" to "inline bool readWav(".
inline bool readWav(const std::string& path,
                    std::vector<float>& outL, std::vector<float>& outR,
                    uint32_t& outRate)
{
  std::FILE* const f = std::fopen(path.c_str(), "rb");
  if (f == nullptr)
    return false;

  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);

  if (size < 44 || size > 64L * 1024 * 1024)
    {
      std::fclose(f);
      return false;
    }

  std::vector<uint8_t> d(static_cast<size_t>(size));
  const size_t got = std::fread(d.data(), 1, d.size(), f);
  std::fclose(f);

  if (got != d.size())
    return false;

  auto rd16 = [&d](size_t o) -> uint32_t
  {
    return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8);
  };
  auto rd32 = [&d](size_t o) -> uint32_t
  {
    return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8)
      | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24);
  };

  if (std::memcmp(&d[0], "RIFF", 4) != 0 || std::memcmp(&d[8], "WAVE", 4) != 0)
    return false;

  uint32_t format = 0, channels = 0, bits = 0, rate = 0;
  size_t   dataOff = 0, dataSize = 0;
  bool     haveFmt = false, haveData = false;

  size_t p = 12;
  while (p + 8 <= d.size())
    {
      const uint32_t csize = rd32(p + 4);
      const size_t   body  = p + 8;

      if (std::memcmp(&d[p], "fmt ", 4) == 0 && csize >= 16 && body + 16 <= d.size())
	{
	  format   = rd16(body);
	  channels = rd16(body + 2);
	  rate     = rd32(body + 4);
	  bits     = rd16(body + 14);

	  // WAVE_FORMAT_EXTENSIBLE: the real format tag is in the sub-format
	  if (format == 0xFFFE && csize >= 26 && body + 26 <= d.size())
	    format = rd16(body + 24);

	  haveFmt = true;
	}
      else if (std::memcmp(&d[p], "data", 4) == 0)
	{
	  dataOff  = body;
	  dataSize = std::min<size_t>(csize, d.size() - body);
	  haveData = true;
	  break;
	}

      const uint64_t next = uint64_t(body) + csize + (csize & 1u);
      if (next >= d.size())
	break;
      p = static_cast<size_t>(next);
    }

  const bool okFormat = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32))
    || (format == 3 && bits == 32);

  if (!haveFmt || !haveData || !okFormat || channels < 1 || rate == 0)
    return false;

  const size_t bytes      = bits / 8;
  const size_t frameBytes = bytes * channels;
  const size_t frames     = dataSize / frameBytes;

  if (frames < 2)
    return false;

  auto sampleAt = [&](size_t o) -> float
  {
    switch (bits)
      {
      case 8:
	return (static_cast<int>(d[o]) - 128) / 128.0f;
      case 16:
	return static_cast<int16_t>(rd16(o)) / 32768.0f;
      case 24:
	{
	  int32_t v = static_cast<int32_t>(uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8)
					   | (uint32_t(d[o + 2]) << 16));
	  if (v & 0x800000)
	    v -= 0x1000000;
	  return v / 8388608.0f;
	}
      case 32:
	if (format == 3)
	  {
	    const uint32_t u = rd32(o);
	    float x;
	    std::memcpy(&x, &u, 4);
	    return x;
	  }
	return static_cast<int32_t>(rd32(o)) / 2147483648.0f;
      }
    return 0.0f;
  };

  outL.assign(frames, 0.0f);
  outR.assign(frames, 0.0f);

  for (size_t i = 0; i < frames; ++i)
    {
      const size_t o = dataOff + i * frameBytes;
      const float  l = sampleAt(o);
      outL[i] = l;
      outR[i] = (channels > 1) ? sampleAt(o + bytes) : l;
    }

  outRate = rate;
  return true;
}

END_NAMESPACE_DISTRHO

#endif
