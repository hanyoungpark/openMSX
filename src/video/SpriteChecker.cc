// $Id$

/*
TODO:
- Speed up checkSpritesN by administrating which lines contain which
  sprites in a bit vector.
  This avoids cycling through all 32 possible sprites on every line.
  Keeping administration up-to-date is not that hard and happens
  at a low frequency (typically once per frame).
- Verify model for 5th sprite number calculation.
  For example, does it have the right value in text mode?
*/

#include "SpriteChecker.hh"
#include "RenderSettings.hh"
#include "BooleanSetting.hh"
#include <algorithm>
#include <cassert>

namespace openmsx {

SpriteChecker::SpriteChecker(VDP& vdp_, RenderSettings& renderSettings,
                             const EmuTime& time)
	: vdp(vdp_), vram(vdp.getVRAM())
	, limitSpritesSetting(renderSettings.getLimitSprites())
	, frameStartTime(time)
{
	vram.spriteAttribTable.setObserver(this);
	vram.spritePatternTable.setObserver(this);
}

void SpriteChecker::reset(const EmuTime& time)
{
	vdp.setSpriteStatus(0); // TODO 0x00 or 0x1F  (blueMSX has 0x1F)
	collisionX = 0;
	collisionY = 0;

	frameStart(time);

	updateSpritesMethod = &SpriteChecker::updateSprites1;
	mode0 = false;
}

static inline SpriteChecker::SpritePattern doublePattern(SpriteChecker::SpritePattern a)
{
	// bit-pattern "abcd...." gets expanded to "aabbccdd"
	// upper 16 bits (of a 32 bit number) contain the pattern
	// lower 16 bits must be zero
	//                               // abcdefghijklmnop0000000000000000
	a = (a | (a >> 8)) & 0xFF00FF00; // abcdefgh00000000ijklmnop00000000
	a = (a | (a >> 4)) & 0xF0F0F0F0; // abcd0000efgh0000ijkl0000mnop0000
	a = (a | (a >> 2)) & 0xCCCCCCCC; // ab00cd00ef00gh00ij00kl00mn00op00
	a = (a | (a >> 1)) & 0xAAAAAAAA; // a0b0c0d0e0f0g0h0i0j0k0l0m0n0o0p0
	return a | (a >> 1);             // aabbccddeeffgghhiijjkkllmmnnoopp
}

inline SpriteChecker::SpritePattern SpriteChecker::calculatePatternNP(
	int patternNr, int y)
{
	const byte* patternPtr = vram.spritePatternTable.getReadArea(0, 256 * 8);
	int index = patternNr * 8 + y;
	SpritePattern pattern = patternPtr[index] << 24;
	if (vdp.getSpriteSize() == 16) {
		pattern |= patternPtr[index + 16] << 16;
	}
	return !vdp.isSpriteMag() ? pattern : doublePattern(pattern);
}
inline SpriteChecker::SpritePattern SpriteChecker::calculatePatternPlanar(
	int patternNr, int y)
{
	const byte* ptr0;
	const byte* ptr1;
	vram.spritePatternTable.getReadAreaPlanar(0, 256 * 8, ptr0, ptr1);
	int index = patternNr * 8 + y;
	const byte* patternPtr = (index & 1) ? ptr1 : ptr0;
	index /= 2;
	SpritePattern pattern = patternPtr[index] << 24;
	if (vdp.getSpriteSize() == 16) {
		pattern |= patternPtr[index + (16 / 2)] << 16;
	}
	return !vdp.isSpriteMag() ? pattern : doublePattern(pattern);
}

inline int SpriteChecker::checkSprites1(
	int line, SpriteChecker::SpriteInfo *visibleSprites)
{
	if (!vdp.needSpriteChecks(line)) {
		return 0;
	}

	// Calculate display line.
	// This is the line sprites are checked at; the line they are displayed
	// at is one lower.
	line = line - vdp.getLineZero() + vdp.getVerticalScroll();

	// Get sprites for this line and detect 5th sprite if any.
	bool limitSprites = limitSpritesSetting.getValue();
	int sprite, visibleIndex = 0;
	int size = vdp.getSpriteSize();
	bool mag = vdp.isSpriteMag();
	int magSize = (mag + 1) * size;
	const byte* attributePtr = vram.spriteAttribTable.getReadArea(0, 32 * 4);
	byte patternIndexMask = size == 16 ? 0xFC : 0xFF;
	for (sprite = 0; sprite < 32; sprite++, attributePtr += 4) {
		int y = *attributePtr;
		if (y == 208) break;
		// Calculate line number within the sprite.
		int spriteLine = (line - y) & 0xFF;
		if (spriteLine < magSize) {
			if (mag) spriteLine /= 2;
			if (visibleIndex == 4) {
				// Five sprites on a line.
				// According to TMS9918.pdf 5th sprite detection is only
				// active when F flag is zero.
				byte status = vdp.getStatusReg0();
				if ((status & 0xC0) == 0) {
					vdp.setSpriteStatus(
					     0x40 | (status & 0x20) | sprite);
				}
				if (limitSprites) break;
			}
			SpriteInfo *sip = &visibleSprites[visibleIndex++];
			int patternIndex = attributePtr[2] & patternIndexMask;
			sip->pattern = calculatePatternNP(patternIndex, spriteLine);
			sip->x = attributePtr[1];
			if (attributePtr[3] & 0x80) sip->x -= 32;
			sip->colourAttrib = attributePtr[3];
		}
	}
	byte status = vdp.getStatusReg0();
	if (~status & 0x40) {
		// No 5th sprite detected, store number of latest sprite processed.
		vdp.setSpriteStatus((status & 0x60) | (std::min(sprite, 31)));
	}

	// Optimisation:
	// If collision already occurred,
	// that state is stable until it is reset by a status reg read,
	// so no need to execute the checks.
	// The visibleSprites array is filled now, so we can bail out.
	if (vdp.getStatusReg0() & 0x20) return visibleIndex;

	/*
	Model for sprite collision: (or "coincidence" in TMS9918 data sheet)
	Reset when status reg is read.
	Set when sprite patterns overlap.
	Colour doesn't matter: sprites of colour 0 can collide.
	Sprites with off-screen position can collide.

	Implemented by checking every pair for collisions.
	For large numbers of sprites that would be slow,
	but there are max 4 sprites and therefore max 6 pairs.
	If any collision is found, method returns at once.
	*/
	for (int i = (visibleIndex < 4 ? visibleIndex : 4); --i >= 1; ) {
		int x_i = visibleSprites[i].x;
		SpritePattern pattern_i = visibleSprites[i].pattern;
		for (int j = i; --j >= 0; ) {
			// Do sprite i and sprite j collide?
			int x_j = visibleSprites[j].x;
			int dist = x_j - x_i;
			if ((-magSize < dist) && (dist < magSize)) {
				SpritePattern pattern_j = visibleSprites[j].pattern;
				if (dist < 0) {
					pattern_j <<= -dist;
				} else {
					pattern_j >>= dist;
				}
				if (pattern_i & pattern_j) {
					// Collision!
					vdp.setSpriteStatus(vdp.getStatusReg0() | 0x20);
					// TODO: Fill in collision coordinates in S#3..S#6.
					// ...Unless this feature only works in sprite mode 2.
					return visibleIndex;
				}
			}
		}
	}

	return visibleIndex;
}

inline int SpriteChecker::checkSprites2(
	int line, SpriteChecker::SpriteInfo *visibleSprites)
{
	if (!vdp.needSpriteChecks(line)) {
		return 0;
	}

	// Calculate display line.
	// This is the line sprites are checked at; the line they are displayed
	// at is one lower.
	line = line - vdp.getLineZero() + vdp.getVerticalScroll();

	// Get sprites for this line and detect 5th sprite if any.
	bool limitSprites = limitSpritesSetting.getValue();
	int sprite, visibleIndex = 0;
	int size = vdp.getSpriteSize();
	bool mag = vdp.isSpriteMag();
	int magSize = (mag + 1) * size;
	int patternIndexMask = (size == 16) ? 0xFC : 0xFF;

	// because it gave a measurable performance boost, we duplicated the
	// code for planar and non-planar modes
	if (planar) {
		const byte* attributePtr0;
		const byte* attributePtr1;
		vram.spriteAttribTable.getReadAreaPlanar(
			512, 32 * 4, attributePtr0, attributePtr1);
		// TODO: Verify CC implementation.
		for (sprite = 0; sprite < 32; ++sprite) {
			int y = attributePtr0[2 * sprite + 0];
			if (y == 216) break;
			// Calculate line number within the sprite.
			int spriteLine = (line - y) & 0xFF;
			if (spriteLine < magSize) {
				if (mag) spriteLine /= 2;
				if (visibleIndex == 8) {
					// Nine sprites on a line.
					// According to TMS9918.pdf 5th sprite detection is only
					// active when F flag is zero. Stuck to this for V9938.
					// Dragon Quest 2 needs this
					byte status = vdp.getStatusReg0();
					if ((status & 0xC0) == 0) {
						vdp.setSpriteStatus(
						     0x40 | (status & 0x20) | sprite);
					}
					if (limitSprites) break;
				}
				int colorIndex = (-1 << 10) | (sprite * 16 + spriteLine);
				byte colorAttrib =
					vram.spriteAttribTable.readPlanar(colorIndex);
				// Sprites with CC=1 are only visible if preceded by
				// a sprite with CC=0.
				if ((colorAttrib & 0x40) && visibleIndex == 0) continue;
				SpriteInfo* sip = &visibleSprites[visibleIndex++];
				int patternIndex = attributePtr0[2 * sprite + 1] & patternIndexMask;
				sip->pattern = calculatePatternPlanar(patternIndex, spriteLine);
				sip->x = attributePtr1[2 * sprite + 0];
				if (colorAttrib & 0x80) sip->x -= 32;
				sip->colourAttrib = colorAttrib;
			}
		}
	} else {
		const byte* attributePtr0 =
			vram.spriteAttribTable.getReadArea(512, 32 * 4);
		// TODO: Verify CC implementation.
		for (sprite = 0; sprite < 32; ++sprite) {
			int y = attributePtr0[4 * sprite + 0];
			if (y == 216) break;
			// Calculate line number within the sprite.
			int spriteLine = (line - y) & 0xFF;
			if (spriteLine < magSize) {
				if (mag) spriteLine /= 2;
				if (visibleIndex == 8) {
					// Nine sprites on a line.
					// According to TMS9918.pdf 5th sprite detection is only
					// active when F flag is zero. Stuck to this for V9938.
					// Dragon Quest 2 needs this
					byte status = vdp.getStatusReg0();
					if ((status & 0xC0) == 0) {
						vdp.setSpriteStatus(
						     0x40 | (status & 0x20) | sprite);
					}
					if (limitSprites) break;
				}
				int colorIndex = (-1 << 10) | (sprite * 16 + spriteLine);
				byte colorAttrib =
					vram.spriteAttribTable.readNP(colorIndex);
				// Sprites with CC=1 are only visible if preceded by
				// a sprite with CC=0.
				if ((colorAttrib & 0x40) && visibleIndex == 0) continue;
				SpriteInfo* sip = &visibleSprites[visibleIndex++];
				int patternIndex = attributePtr0[4 * sprite + 2] & patternIndexMask;
				sip->pattern = calculatePatternNP(patternIndex, spriteLine);
				sip->x = attributePtr0[4 * sprite + 1];
				if (colorAttrib & 0x80) sip->x -= 32;
				sip->colourAttrib = colorAttrib;
			}
		}
	}
	byte status = vdp.getStatusReg0();
	if (~status & 0x40) {
		// No 9th sprite detected, store number of latest sprite processed.
		vdp.setSpriteStatus((status & 0x60) | (std::min(sprite, 31)));
	}

	// Optimisation:
	// If collision already occurred,
	// that state is stable until it is reset by a status reg read,
	// so no need to execute the checks.
	// The visibleSprites array is filled now, so we can bail out.
	if (vdp.getStatusReg0() & 0x20) return visibleIndex;

	/*
	Model for sprite collision: (or "coincidence" in TMS9918 data sheet)
	Reset when status reg is read.
	Set when sprite patterns overlap.
	Colour doesn't matter: sprites of colour 0 can collide.
	  TODO: V9938 data book denies this (page 98).
	Sprites with off-screen position can collide.

	Implemented by checking every pair for collisions.
	For large numbers of sprites that would be slow.
	There are max 8 sprites and therefore max 42 pairs.
	  TODO: Maybe this is slow... Think of something faster.
	        Probably new approach is needed anyway for OR-ing.
	If any collision is found, method returns at once.
	*/
	for (int i = (visibleIndex < 8 ? visibleIndex : 8); --i >= 1; ) {
		// If CC or IC is set, this sprite cannot collide.
		if (visibleSprites[i].colourAttrib & 0x60) continue;

		int x_i = visibleSprites[i].x;
		SpritePattern pattern_i = visibleSprites[i].pattern;
		for (int j = i; --j >= 0; ) {
			// If CC or IC is set, this sprite cannot collide.
			if (visibleSprites[j].colourAttrib & 0x60) continue;

			// Do sprite i and sprite j collide?
			int x_j = visibleSprites[j].x;
			int dist = x_j - x_i;
			if ((-magSize < dist) && (dist < magSize)) {
				SpritePattern pattern_j = visibleSprites[j].pattern;
				if (dist < 0) {
					pattern_j <<= -dist;
				} else {
					pattern_j >>= dist;
				}
				if (pattern_i & pattern_j) {
					// Collision!
					vdp.setSpriteStatus(vdp.getStatusReg0() | 0x20);
					// TODO: Fill in collision coordinates in S#3..S#6.
					//       See page 97 for info.
					// TODO: I guess the VDP checks for collisions while
					//       scanning, if so the top-leftmost collision
					//       should be remembered. Currently the topmost
					//       line is guaranteed, but within that line
					//       the highest sprite numbers are selected.
					return visibleIndex;
				}
			}
		}
	}

	return visibleIndex;
}

void SpriteChecker::updateSprites0(int /*limit*/)
{
	// If this method is called, that means somewhere a check for sprite
	// mode 0 is missing and performance is being wasted.
	// The updateSpritesN methods are called by checkUntil, which is
	// documented as not allowed to be called in sprite mode 0.
	assert(false);
}

void SpriteChecker::updateSprites1(int limit)
{
	while (currentLine < limit) {
		spriteCount[currentLine] =
			checkSprites1(currentLine, spriteBuffer[currentLine]);
		currentLine++;
	}
}

void SpriteChecker::updateSprites2(int limit)
{
	/*
	cout << "updateSprites2: until = " << until
		<< ", frameStart = " << frameStartTime << "\n";
	*/
	while (currentLine < limit) {
		spriteCount[currentLine] =
			checkSprites2(currentLine, spriteBuffer[currentLine]);
		currentLine++;
	}
}

} // namespace openmsx

