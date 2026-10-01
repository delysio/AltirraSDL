// OpenType table layouts: https://learn.microsoft.com/typography/opentype/spec/
// name (UTF-16BE and IDs 1/2/16/17), post (fixed pitch and italic angle),
// otff (SFNT/TTC offsets), fvar (named instances).
#include "ui_font_metadata.h"

#include <cstdint>
#include <map>

namespace {
	struct Bytes {
		const uint8_t *data = nullptr;
		size_t size = 0;
		bool Has(size_t offset, size_t length) const {
			return offset <= size && length <= size - offset;
		}
		uint16_t U16(size_t offset) const {
			return Has(offset, 2) ? (uint16_t)((data[offset] << 8)
				| data[offset + 1]) : 0;
		}
		uint32_t U32(size_t offset) const {
			return Has(offset, 4) ? ((uint32_t)U16(offset) << 16)
				| U16(offset + 2) : 0;
		}
		Bytes Slice(size_t offset, size_t length) const {
			return Has(offset, length) ? Bytes{data + offset, length} : Bytes{};
		}
	};

	void AppendUTF8(std::string& out, uint32_t cp) {
		if (cp < 0x80) out += (char)cp;
		else {
			if (cp >= 0x10000) out += (char)(0xF0 | (cp >> 18));
			else if (cp >= 0x800) out += (char)(0xE0 | (cp >> 12));
			else out += (char)(0xC0 | (cp >> 6));
			if (cp >= 0x10000) out += (char)(0x80 | ((cp >> 12) & 63));
			if (cp >= 0x800) out += (char)(0x80 | ((cp >> 6) & 63));
			out += (char)(0x80 | (cp & 63));
		}
	}

	std::string DecodeName(Bytes bytes, bool unicode) {
		std::string result;
		if (!unicode) {
			// Apple Roman mapping: unicode.org/Public/MAPPINGS/VENDORS/APPLE/ROMAN.TXT
			static constexpr uint16_t kMacRoman[] = {
				0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
				0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
				0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
				0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
				0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
				0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
				0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
				0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
				0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
				0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
				0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
				0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
				0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
				0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
				0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
				0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
			};
			for (size_t i = 0; i < bytes.size; ++i) {
				const uint8_t ch = bytes.data[i];
				if (!ch) return {};
				AppendUTF8(result, ch < 128 ? ch : kMacRoman[ch - 128]);
			}
			return result;
		}
		if (bytes.size % 2) return {};
		for (size_t i = 0; i < bytes.size; i += 2) {
			uint32_t cp = bytes.U16(i);
			if (!cp) return {};
			if (cp >= 0xD800 && cp <= 0xDBFF) {
				if (!bytes.Has(i + 2, 2)) return {};
				const uint32_t low = bytes.U16(i + 2);
				if (low < 0xDC00 || low > 0xDFFF) return {};
				cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00;
				i += 2;
			} else if (cp >= 0xDC00 && cp <= 0xDFFF) return {};
			AppendUTF8(result, cp);
		}
		return result;
	}

	std::map<uint16_t, std::string> ReadNames(Bytes names) {
		std::map<uint16_t, std::string> result;
		std::map<uint16_t, int> scores;
		if (!names.Has(0, 6) || names.U16(0) > 1) return result;
		const size_t count = names.U16(2), storage = names.U16(4);
		if (!names.Has(6, count * 12)) return result;
		for (size_t i = 0; i < count; ++i) {
			const size_t p = 6 + i * 12;
			const auto platform = names.U16(p), encoding = names.U16(p + 2);
			const auto language = names.U16(p + 4), id = names.U16(p + 6);
			const bool unicode = platform == 0 || (platform == 3
				&& (encoding == 0 || encoding == 1 || encoding == 10));
			if (!unicode && !(platform == 1 && encoding == 0)) continue;
			const int score = (unicode ? 2 : 0)
				+ (platform == 3 && language == 0x409 ? 2 : 0);
			if (result.count(id) && scores[id] >= score) continue;
			auto text = DecodeName(names.Slice(storage + names.U16(p + 10),
				names.U16(p + 8)), unicode);
			if (text.empty()) continue;
			result[id] = std::move(text);
			scores[id] = score;
		}
		return result;
	}
}

std::vector<ATUIFontFace> ATUIReadFontMetadata(const void *data, size_t size,
	bool includeNamedInstances)
{
	std::vector<ATUIFontFace> result;
	if (!data) return result;
	const Bytes file{(const uint8_t *)data, size};
	const bool collection = file.U32(0) == 0x74746366; // ttcf
	const size_t count = collection ? file.U32(8) : 1;
	if (!count || count > 65536 || (collection && !file.Has(12, count * 4)))
		return result;
	for (size_t face = 0; face < count; ++face) {
		const size_t offset = collection ? file.U32(12 + face * 4) : 0;
		const auto signature = file.U32(offset);
		if (signature != 0x10000 && signature != 0x4F54544F
			&& signature != 0x74727565) continue; // TrueType, OTTO, true
		if (!file.Has(offset, 12)) continue;
		const size_t tables = file.U16(offset + 4);
		if (!file.Has(offset + 12, tables * 16)) continue;
		std::map<uint32_t, Bytes> directory;
		for (size_t i = 0; i < tables; ++i) {
			const size_t p = offset + 12 + i * 16;
			directory[file.U32(p)] = file.Slice(file.U32(p + 8), file.U32(p + 12));
		}
		// Ignore incomplete files before they reach the rasterizer.
		if (!directory[0x636D6170].size || !directory[0x68656164].size
			|| !directory[0x6D617870].size) continue; // cmap, head, maxp
		auto names = ReadNames(directory[0x6E616D65]);
		ATUIFontFace record;
		record.family = names.count(16) ? names[16] : names[1];
		record.style = names.count(17) ? names[17] : names[2];
		if (record.family.empty()) continue;
		if (record.style.empty()) record.style = "Regular";
		record.index = (int)face;
		record.requiresNativeRenderer = directory[0x43464632].size != 0; // CFF2
		const auto post = directory[0x706F7374];
		record.monospaced = post.U32(12) != 0;
		record.italic = post.U32(4) != 0;
		result.push_back(record);
		if (!includeNamedInstances) continue;
		const auto fvar = directory[0x66766172];
		if (!fvar.Has(0, 16) || fvar.U16(0) != 1) continue;
		const size_t axisCount = fvar.U16(8), axisSize = fvar.U16(10);
		const size_t instanceCount = fvar.U16(12), instanceSize = fvar.U16(14);
		const size_t instances = fvar.U16(4) + axisCount * axisSize;
		if (axisSize < 20 || instanceSize < 4 + axisCount * 4
			|| instanceCount > 32767 || !fvar.Has(instances, instanceCount * instanceSize))
			continue;
		for (size_t i = 0; i < instanceCount; ++i) {
			const auto id = fvar.U16(instances + i * instanceSize);
			if (!names.count(id)) continue;
			record.style = names[id];
			// FreeType face index bits 16–30 select the 1-based named instance.
			record.index = (int)(face | ((i + 1) << 16));
			result.push_back(record);
		}
	}
	return result;
}
