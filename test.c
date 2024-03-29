#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clowncommon/clowncommon.h"

#include "compress.h"
#include "decompress.h"

typedef struct MemoryStream
{
	size_t size, read_index, write_index;
	unsigned char *buffer;
} MemoryStream;

static void MemoryStream_Initialise(MemoryStream* const stream)
{
	stream->size = stream->read_index = stream->write_index = 0;
	stream->buffer = NULL;
}

static void MemoryStream_Deinitialise(MemoryStream* const stream)
{
	free(stream->buffer);
}

static void MemoryStream_Clear(MemoryStream* const stream)
{
	stream->read_index = stream->write_index = 0;
}

static int ReadByteFromFile(void* const user_data)
{
	FILE* const file = (FILE*)user_data;
	const int value = fgetc(file);

	if (value == EOF)
	{
		if (ferror(file))
			return CLOWNNEMESIS_ERROR;

		rewind(file);
		return CLOWNNEMESIS_EOF;
	}

	return value;
}

static int ReadByteFromMemoryStream(void* const user_data)
{
	MemoryStream* const stream = (MemoryStream*)user_data;

	if (stream->read_index == stream->write_index)
	{
		stream->read_index = 0;
		return CLOWNNEMESIS_EOF;
	}

	return stream->buffer[stream->read_index++];
}

static int WriteByteToMemoryStream(void* const user_data, const unsigned char byte)
{
	MemoryStream* const stream = (MemoryStream*)user_data;

	if (stream->write_index == stream->size)
	{
		unsigned char *new_buffer;

		const size_t new_size = stream->size == 0 ? 1 : stream->size * 2;

		/* Detect overflow. */
		if (stream->size > (size_t)-1 / 2)
			return CLOWNNEMESIS_ERROR;

		new_buffer = (unsigned char*)realloc(stream->buffer, new_size);

		if (new_buffer == NULL)
			return CLOWNNEMESIS_ERROR;

		stream->buffer = new_buffer;
		stream->size = new_size;
	}

	stream->buffer[stream->write_index++] = byte;

	return byte;
}

static void DoTests(const cc_bool accurate)
{
	size_t total_uncompressed_size, total_original_compressed_size, total_new_compressed_size;
	MemoryStream compressed_memory_stream, decompressed_memory_stream, compressed_memory_stream_2, decompressed_memory_stream_2;
	size_t i;

	static const char* const files[] = {
		"tests/s1disasm/artnem/8x8 - GHZ1.nem",
		"tests/s1disasm/artnem/8x8 - GHZ2.nem",
		"tests/s1disasm/artnem/8x8 - LZ.nem",
		"tests/s1disasm/artnem/8x8 - MZ.nem",
		"tests/s1disasm/artnem/8x8 - SBZ.nem",
		"tests/s1disasm/artnem/8x8 - SLZ.nem",
		"tests/s1disasm/artnem/8x8 - SYZ.nem",
		"tests/s1disasm/artnem/Animal Chicken.nem",
		"tests/s1disasm/artnem/Animal Flicky.nem",
		"tests/s1disasm/artnem/Animal Penguin.nem",
		"tests/s1disasm/artnem/Animal Pig.nem",
		"tests/s1disasm/artnem/Animal Rabbit.nem",
		"tests/s1disasm/artnem/Animal Seal.nem",
		"tests/s1disasm/artnem/Animal Squirrel.nem",
		"tests/s1disasm/artnem/Boss - Eggman after FZ Fight.nem",
		"tests/s1disasm/artnem/Boss - Eggman in SBZ2 & FZ.nem",
		"tests/s1disasm/artnem/Boss - Exhaust Flame.nem",
		"tests/s1disasm/artnem/Boss - Final Zone.nem",
		"tests/s1disasm/artnem/Boss - Main.nem",
		"tests/s1disasm/artnem/Boss - Weapons.nem",
		"tests/s1disasm/artnem/Continue Screen Sonic.nem",
		"tests/s1disasm/artnem/Continue Screen Stuff.nem",
		"tests/s1disasm/artnem/Ending - Credits.nem",
		"tests/s1disasm/artnem/Ending - Emeralds.nem",
		"tests/s1disasm/artnem/Ending - Flowers.nem",
		"tests/s1disasm/artnem/Ending - Sonic.nem",
		"tests/s1disasm/artnem/Ending - StH Logo.nem",
		"tests/s1disasm/artnem/Ending - Try Again.nem",
		"tests/s1disasm/artnem/Enemy Ball Hog.nem",
		"tests/s1disasm/artnem/Enemy Basaran.nem",
		"tests/s1disasm/artnem/Enemy Bomb.nem",
		"tests/s1disasm/artnem/Enemy Burrobot.nem",
		"tests/s1disasm/artnem/Enemy Buzz Bomber.nem",
		"tests/s1disasm/artnem/Enemy Caterkiller.nem",
		"tests/s1disasm/artnem/Enemy Chopper.nem",
		"tests/s1disasm/artnem/Enemy Crabmeat.nem",
		"tests/s1disasm/artnem/Enemy Jaws.nem",
		"tests/s1disasm/artnem/Enemy Motobug.nem",
		"tests/s1disasm/artnem/Enemy Newtron.nem",
		"tests/s1disasm/artnem/Enemy Orbinaut.nem",
		"tests/s1disasm/artnem/Enemy Roller.nem",
		"tests/s1disasm/artnem/Enemy Splats.nem",
		"tests/s1disasm/artnem/Enemy Yadrin.nem",
		"tests/s1disasm/artnem/Explosion.nem",
		"tests/s1disasm/artnem/Fireballs.nem",
		"tests/s1disasm/artnem/GHZ Breakable Wall.nem",
		"tests/s1disasm/artnem/GHZ Bridge.nem",
		"tests/s1disasm/artnem/GHZ Edge Wall.nem",
		"tests/s1disasm/artnem/GHZ Flower Stalk.nem",
		"tests/s1disasm/artnem/GHZ Giant Ball.nem",
		"tests/s1disasm/artnem/GHZ Purple Rock.nem",
		"tests/s1disasm/artnem/GHZ Spiked Log.nem",
		"tests/s1disasm/artnem/GHZ Swinging Platform.nem",
		"tests/s1disasm/artnem/Game Over.nem",
		"tests/s1disasm/artnem/Giant Ring Flash.nem",
		"tests/s1disasm/artnem/HUD - Life Counter Icon.nem",
		"tests/s1disasm/artnem/HUD.nem",
		"tests/s1disasm/artnem/Hidden Bonuses.nem",
		"tests/s1disasm/artnem/Hidden Japanese Credits.nem",
		"tests/s1disasm/artnem/Invincibility Stars.nem",
		"tests/s1disasm/artnem/LZ 32x16 Block.nem",
		"tests/s1disasm/artnem/LZ 32x32 Block.nem",
		"tests/s1disasm/artnem/LZ Blocks.nem",
		"tests/s1disasm/artnem/LZ Breakable Pole.nem",
		"tests/s1disasm/artnem/LZ Bubbles & Countdown.nem",
		"tests/s1disasm/artnem/LZ Cork.nem",
		"tests/s1disasm/artnem/LZ Flapping Door.nem",
		"tests/s1disasm/artnem/LZ Gargoyle & Fireball.nem",
		"tests/s1disasm/artnem/LZ Harpoon.nem",
		"tests/s1disasm/artnem/LZ Horizontal Door.nem",
		"tests/s1disasm/artnem/LZ Rising Platform.nem",
		"tests/s1disasm/artnem/LZ Spiked Ball & Chain.nem",
		"tests/s1disasm/artnem/LZ Vertical Door.nem",
		"tests/s1disasm/artnem/LZ Water & Splashes.nem",
		"tests/s1disasm/artnem/LZ Water Surface.nem",
		"tests/s1disasm/artnem/LZ Wheel.nem",
		"tests/s1disasm/artnem/Lamppost.nem",
		"tests/s1disasm/artnem/MZ Green Glass Block.nem",
		"tests/s1disasm/artnem/MZ Green Pushable Block.nem",
		"tests/s1disasm/artnem/MZ Lava.nem",
		"tests/s1disasm/artnem/MZ Metal Blocks.nem",
		"tests/s1disasm/artnem/MZ Switch.nem",
		"tests/s1disasm/artnem/Monitors.nem",
		"tests/s1disasm/artnem/Points.nem",
		"tests/s1disasm/artnem/Prison Capsule.nem",
		"tests/s1disasm/artnem/Rings.nem",
		"tests/s1disasm/artnem/SBZ Collapsing Floor.nem",
		"tests/s1disasm/artnem/SBZ Crushing Girder.nem",
		"tests/s1disasm/artnem/SBZ Electrocuter.nem",
		"tests/s1disasm/artnem/SBZ Flaming Pipe.nem",
		"tests/s1disasm/artnem/SBZ Junction Wheel.nem",
		"tests/s1disasm/artnem/SBZ Large Horizontal Door.nem",
		"tests/s1disasm/artnem/SBZ Pizza Cutter.nem",
		"tests/s1disasm/artnem/SBZ Running Disc.nem",
		"tests/s1disasm/artnem/SBZ Sliding Floor Trap.nem",
		"tests/s1disasm/artnem/SBZ Small Vertical Door.nem",
		"tests/s1disasm/artnem/SBZ Spinning Platform.nem",
		"tests/s1disasm/artnem/SBZ Stomper.nem",
		"tests/s1disasm/artnem/SBZ Trapdoor.nem",
		"tests/s1disasm/artnem/SBZ Vanishing Block.nem",
		"tests/s1disasm/artnem/SLZ 32x32 Block.nem",
		"tests/s1disasm/artnem/SLZ Breakable Wall.nem",
		"tests/s1disasm/artnem/SLZ Cannon.nem",
		"tests/s1disasm/artnem/SLZ Fan.nem",
		"tests/s1disasm/artnem/SLZ Little Spikeball.nem",
		"tests/s1disasm/artnem/SLZ Pylon.nem",
		"tests/s1disasm/artnem/SLZ Seesaw.nem",
		"tests/s1disasm/artnem/SLZ Swinging Platform.nem",
		"tests/s1disasm/artnem/SYZ Bumper.nem",
		"tests/s1disasm/artnem/SYZ Large Spikeball.nem",
		"tests/s1disasm/artnem/SYZ Small Spikeball.nem",
		"tests/s1disasm/artnem/Sega Logo (JP1).nem",
		"tests/s1disasm/artnem/Sega Logo.nem",
		"tests/s1disasm/artnem/Shield.nem",
		"tests/s1disasm/artnem/Signpost.nem",
		"tests/s1disasm/artnem/Special 1UP.nem",
		"tests/s1disasm/artnem/Special Birds & Fish.nem",
		"tests/s1disasm/artnem/Special Clouds.nem",
		"tests/s1disasm/artnem/Special Emerald Twinkle.nem",
		"tests/s1disasm/artnem/Special Emeralds.nem",
		"tests/s1disasm/artnem/Special GOAL.nem",
		"tests/s1disasm/artnem/Special Ghost.nem",
		"tests/s1disasm/artnem/Special Glass.nem",
		"tests/s1disasm/artnem/Special R.nem",
		"tests/s1disasm/artnem/Special Red-White.nem",
		"tests/s1disasm/artnem/Special Result Emeralds.nem",
		"tests/s1disasm/artnem/Special UP-DOWN.nem",
		"tests/s1disasm/artnem/Special W.nem",
		"tests/s1disasm/artnem/Special Walls.nem",
		"tests/s1disasm/artnem/Special ZONE1.nem",
		"tests/s1disasm/artnem/Special ZONE2.nem",
		"tests/s1disasm/artnem/Special ZONE3.nem",
		"tests/s1disasm/artnem/Special ZONE4.nem",
		"tests/s1disasm/artnem/Special ZONE5.nem",
		"tests/s1disasm/artnem/Special ZONE6.nem",
		"tests/s1disasm/artnem/Spikes.nem",
		"tests/s1disasm/artnem/Spring Horizontal.nem",
		"tests/s1disasm/artnem/Spring Vertical.nem",
		"tests/s1disasm/artnem/Switch.nem",
		"tests/s1disasm/artnem/Title Cards.nem",
		"tests/s1disasm/artnem/Title Screen Foreground.nem",
		"tests/s1disasm/artnem/Title Screen Sonic.nem",
		"tests/s1disasm/artnem/Title Screen TM.nem",
		"tests/s1disasm/artnem/Unused - Eggman Ending.nem",
		"tests/s1disasm/artnem/Unused - Explosion.nem",
		"tests/s1disasm/artnem/Unused - Fireball.nem",
		"tests/s1disasm/artnem/Unused - GHZ Block.nem",
		"tests/s1disasm/artnem/Unused - GHZ Log.nem",
		"tests/s1disasm/artnem/Unused - Goggles.nem",
		"tests/s1disasm/artnem/Unused - Grass.nem",
		"tests/s1disasm/artnem/Unused - LZ Sonic.nem",
		"tests/s1disasm/artnem/Unused - MZ Background.nem",
		"tests/s1disasm/artnem/Unused - SStage Flash.nem",
		"tests/s1disasm/artnem/Unused - SYZ Sparkles.nem",
		"tests/s1disasm/artnem/Unused - Smoke.nem",
		"tests/s2disasm/art/nemesis/1P and 2P wins text from 2P mode.nem",
		"tests/s2disasm/art/nemesis/1Player2VS.nem",
		"tests/s2disasm/art/nemesis/4 stripy blocks from OOZ.nem",
		"tests/s2disasm/art/nemesis/A few menu blocks.nem",
		"tests/s2disasm/art/nemesis/A menu box with a shadow.nem",
		"tests/s2disasm/art/nemesis/ARZ boss.nem",
		"tests/s2disasm/art/nemesis/Arrow shooter and arrow from ARZ.nem",
		"tests/s2disasm/art/nemesis/Background art for special stage.nem",
		"tests/s2disasm/art/nemesis/Balkrie (jet badnik) from SCZ.nem",
		"tests/s2disasm/art/nemesis/Ball on spring from OOZ (beta holdovers).nem",
		"tests/s2disasm/art/nemesis/Bear.nem",
		"tests/s2disasm/art/nemesis/Blowfly from ARZ.nem",
		"tests/s2disasm/art/nemesis/Bolt end and rope from MTZ.nem",
		"tests/s2disasm/art/nemesis/Bomb from special stage.nem",
		"tests/s2disasm/art/nemesis/Bomber badnik from SCZ.nem",
		"tests/s2disasm/art/nemesis/Bouncer badnik from CNZ.nem",
		"tests/s2disasm/art/nemesis/Breakaway panels from WFZ.nem",
		"tests/s2disasm/art/nemesis/Bubble generator.nem",
		"tests/s2disasm/art/nemesis/Bubbles.nem",
		"tests/s2disasm/art/nemesis/Burner Platform from OOZ.nem",
		"tests/s2disasm/art/nemesis/Button.nem",
		"tests/s2disasm/art/nemesis/Buzzer enemy.nem",
		"tests/s2disasm/art/nemesis/CNZ boss.nem",
		"tests/s2disasm/art/nemesis/CNZ elevator.nem",
		"tests/s2disasm/art/nemesis/CNZ slot machine bars.nem",
		"tests/s2disasm/art/nemesis/CPZ boss.nem",
		"tests/s2disasm/art/nemesis/CPZ large moving platform blocks.nem",
		"tests/s2disasm/art/nemesis/CPZ metal things.nem",
		"tests/s2disasm/art/nemesis/CPZ spintube exit cover.nem",
		"tests/s2disasm/art/nemesis/CPZ worm enemy.nem",
		"tests/s2disasm/art/nemesis/Cascading oil from OOZ.nem",
		"tests/s2disasm/art/nemesis/Cascading oil hitting oil from OOZ.nem",
		"tests/s2disasm/art/nemesis/Catapult that shoots Sonic to the side from WFZ.nem",
		"tests/s2disasm/art/nemesis/Caterpiller platforms from CNZ.nem",
		"tests/s2disasm/art/nemesis/Chicken.nem",
		"tests/s2disasm/art/nemesis/Chopper blades for EHZ boss.nem",
		"tests/s2disasm/art/nemesis/Clouds.nem",
		"tests/s2disasm/art/nemesis/Coconuts badnik from EHZ.nem",
		"tests/s2disasm/art/nemesis/Collapsing platform from MCZ.nem",
		"tests/s2disasm/art/nemesis/Credit Text.nem",
		"tests/s2disasm/art/nemesis/Diagonal impulse spring from CNZ.nem",
		"tests/s2disasm/art/nemesis/Diagonal shadow from special stage.nem",
		"tests/s2disasm/art/nemesis/Diagonal spring.nem",
		"tests/s2disasm/art/nemesis/Drawbridge logs from MCZ.nem",
		"tests/s2disasm/art/nemesis/Driller badnik from HTZ.nem",
		"tests/s2disasm/art/nemesis/Drop target from CNZ.nem",
		"tests/s2disasm/art/nemesis/Dynamically reloaded cliffs in HTZ background.nem",
		"tests/s2disasm/art/nemesis/EHZ Pirahna badnik.nem",
		"tests/s2disasm/art/nemesis/EHZ boss.nem",
		"tests/s2disasm/art/nemesis/EHZ bridge.nem",
		"tests/s2disasm/art/nemesis/Eagle.nem",
		"tests/s2disasm/art/nemesis/Egg Prison.nem",
		"tests/s2disasm/art/nemesis/Eggpod.nem",
		"tests/s2disasm/art/nemesis/Eggrobo.nem",
		"tests/s2disasm/art/nemesis/Emerald from special stage.nem",
		"tests/s2disasm/art/nemesis/End of level results text.nem",
		"tests/s2disasm/art/nemesis/Exploding star badnik from MTZ.nem",
		"tests/s2disasm/art/nemesis/Explosion from special stage.nem",
		"tests/s2disasm/art/nemesis/Explosion.nem",
		"tests/s2disasm/art/nemesis/Fan from OOZ.nem",
		"tests/s2disasm/art/nemesis/Final image of Tails.nem",
		"tests/s2disasm/art/nemesis/Final image of Tornado with it and Sonic facing screen.nem",
		"tests/s2disasm/art/nemesis/Fireball 1.nem",
		"tests/s2disasm/art/nemesis/Fireball 2.nem",
		"tests/s2disasm/art/nemesis/Fireball 3.nem",
		"tests/s2disasm/art/nemesis/Firefly from MCZ.nem",
		"tests/s2disasm/art/nemesis/Flicky.nem",
		"tests/s2disasm/art/nemesis/Flippers.nem",
		"tests/s2disasm/art/nemesis/Font using large broken letters.nem",
		"tests/s2disasm/art/nemesis/Game and Time Over text.nem",
		"tests/s2disasm/art/nemesis/Green flame from OOZ burners.nem",
		"tests/s2disasm/art/nemesis/Grounder from ARZ.nem",
		"tests/s2disasm/art/nemesis/HTZ boss.nem",
		"tests/s2disasm/art/nemesis/HTZ zip-line platform.nem",
		"tests/s2disasm/art/nemesis/HUD.nem",
		"tests/s2disasm/art/nemesis/Hexagonal bumper from CNZ.nem",
		"tests/s2disasm/art/nemesis/Hook on chain from WFZ.nem",
		"tests/s2disasm/art/nemesis/Horizontal jet.nem",
		"tests/s2disasm/art/nemesis/Horizontal shadow from special stage.nem",
		"tests/s2disasm/art/nemesis/Horizontal spinning blades in WFZ.nem",
		"tests/s2disasm/art/nemesis/Horizontal spring.nem",
		"tests/s2disasm/art/nemesis/Invincibility stars.nem",
		"tests/s2disasm/art/nemesis/Large explosion.nem",
		"tests/s2disasm/art/nemesis/Large moving platform from CPZ.nem",
		"tests/s2disasm/art/nemesis/Large spinning wheel from MTZ - indent.nem",
		"tests/s2disasm/art/nemesis/Large spinning wheel from MTZ.nem",
		"tests/s2disasm/art/nemesis/Large wooden box from MCZ.nem",
		"tests/s2disasm/art/nemesis/Lava bubble from MTZ.nem",
		"tests/s2disasm/art/nemesis/Lava cup from MTZ.nem",
		"tests/s2disasm/art/nemesis/Leaves in ARZ.nem",
		"tests/s2disasm/art/nemesis/Lever spring.nem",
		"tests/s2disasm/art/nemesis/Long horizontal spike.nem",
		"tests/s2disasm/art/nemesis/MCZ boss.nem",
		"tests/s2disasm/art/nemesis/MTZ boss.nem",
		"tests/s2disasm/art/nemesis/MTZ spike block.nem",
		"tests/s2disasm/art/nemesis/Main patterns from title screen.nem",
		"tests/s2disasm/art/nemesis/Miles life counter.nem",
		"tests/s2disasm/art/nemesis/Monitor and contents.nem",
		"tests/s2disasm/art/nemesis/Monkey.nem",
		"tests/s2disasm/art/nemesis/Mouse.nem",
		"tests/s2disasm/art/nemesis/Movie sequence at end of game.nem",
		"tests/s2disasm/art/nemesis/Moving block from CNZ and CPZ.nem",
		"tests/s2disasm/art/nemesis/Moving block from CPZ.nem",
		"tests/s2disasm/art/nemesis/Moving platform from WFZ.nem",
		"tests/s2disasm/art/nemesis/Numbers.nem",
		"tests/s2disasm/art/nemesis/OOZ boss.nem",
		"tests/s2disasm/art/nemesis/OOZ collapsing platform.nem",
		"tests/s2disasm/art/nemesis/Octopus badnik from OOZ.nem",
		"tests/s2disasm/art/nemesis/One way barrier from ARZ.nem",
		"tests/s2disasm/art/nemesis/One way barrier from HTZ.nem",
		"tests/s2disasm/art/nemesis/Penguin.nem",
		"tests/s2disasm/art/nemesis/Perfect text.nem",
		"tests/s2disasm/art/nemesis/Pictures in level preview box from level select.nem",
		"tests/s2disasm/art/nemesis/Pig.nem",
		"tests/s2disasm/art/nemesis/Platform on belt in WFZ.nem",
		"tests/s2disasm/art/nemesis/Praying mantis badnik from MTZ.nem",
		"tests/s2disasm/art/nemesis/Pull switch from MCZ.nem",
		"tests/s2disasm/art/nemesis/Push spring from OOZ.nem",
		"tests/s2disasm/art/nemesis/Rabbit.nem",
		"tests/s2disasm/art/nemesis/Red horizontal laser from WFZ.nem",
		"tests/s2disasm/art/nemesis/Retracting platform from WFZ.nem",
		"tests/s2disasm/art/nemesis/Rexxon (lava snake) from HTZ.nem",
		"tests/s2disasm/art/nemesis/Ring.nem",
		"tests/s2disasm/art/nemesis/Rising platform from OOZ.nem",
		"tests/s2disasm/art/nemesis/Robotnik's head.nem",
		"tests/s2disasm/art/nemesis/Robotnik's lower half.nem",
		"tests/s2disasm/art/nemesis/Robotnik.nem",
		"tests/s2disasm/art/nemesis/Rock from HTZ.nem",
		"tests/s2disasm/art/nemesis/Rocket thruster for Tornado.nem",
		"tests/s2disasm/art/nemesis/Round bumper from CNZ.nem",
		"tests/s2disasm/art/nemesis/SEGA.nem",
		"tests/s2disasm/art/nemesis/Scratch from WFZ.nem",
		"tests/s2disasm/art/nemesis/Seahorse from OOZ.nem",
		"tests/s2disasm/art/nemesis/Seal.nem",
		"tests/s2disasm/art/nemesis/See-saw in HTZ.nem",
		"tests/s2disasm/art/nemesis/Shaded blocks from intro.nem",
		"tests/s2disasm/art/nemesis/Shark from ARZ.nem",
		"tests/s2disasm/art/nemesis/Shellcracker badnik from MTZ.nem",
		"tests/s2disasm/art/nemesis/Shield.nem",
		"tests/s2disasm/art/nemesis/Signpost.nem",
		"tests/s2disasm/art/nemesis/Silver Sonic.nem",
		"tests/s2disasm/art/nemesis/Similarly shaded blocks from MTZ.nem",
		"tests/s2disasm/art/nemesis/Small cog from MTZ.nem",
		"tests/s2disasm/art/nemesis/Small pictures of Sonic and final image of Sonic in Super Sonic mode.nem",
		"tests/s2disasm/art/nemesis/Small pictures of Sonic and final image of Sonic.nem",
		"tests/s2disasm/art/nemesis/Small pictures of Tornado in final ending sequence.nem",
		"tests/s2disasm/art/nemesis/Small yellow moving platform from CPZ.nem",
		"tests/s2disasm/art/nemesis/Smoke trail from CPZ and HTZ bosses.nem",
		"tests/s2disasm/art/nemesis/Snake badnik from MCZ.nem",
		"tests/s2disasm/art/nemesis/Sol badnik from HTZ.nem",
		"tests/s2disasm/art/nemesis/Sonic and Miles number text from special stage.nem",
		"tests/s2disasm/art/nemesis/Sonic and Tails animation frames in special stage.nem",
		"tests/s2disasm/art/nemesis/Sonic and Tails from title screen.nem",
		"tests/s2disasm/art/nemesis/Sonic continue.nem",
		"tests/s2disasm/art/nemesis/Sonic lives counter.nem",
		"tests/s2disasm/art/nemesis/Sonic the Hedgehog 2 image at end of credits.nem",
		"tests/s2disasm/art/nemesis/Special stage Player VS Player text.nem",
		"tests/s2disasm/art/nemesis/Special stage messages and icons.nem",
		"tests/s2disasm/art/nemesis/Special stage results screen art and some emeralds.nem",
		"tests/s2disasm/art/nemesis/Special stage ring art.nem",
		"tests/s2disasm/art/nemesis/Speed booster from CPZ.nem",
		"tests/s2disasm/art/nemesis/Spider badnik from CPZ.nem",
		"tests/s2disasm/art/nemesis/Spike from MTZ.nem",
		"tests/s2disasm/art/nemesis/Spiked ball from OOZ.nem",
		"tests/s2disasm/art/nemesis/Spikes.nem",
		"tests/s2disasm/art/nemesis/Spikey ball from CNZ slots.nem",
		"tests/s2disasm/art/nemesis/Spin tube flash from MTZ.nem",
		"tests/s2disasm/art/nemesis/Squirrel.nem",
		"tests/s2disasm/art/nemesis/Standard font.nem",
		"tests/s2disasm/art/nemesis/Star pole.nem",
		"tests/s2disasm/art/nemesis/Stars in special stage.nem",
		"tests/s2disasm/art/nemesis/Start text from special stage.nem",
		"tests/s2disasm/art/nemesis/Steam from MTZ.nem",
		"tests/s2disasm/art/nemesis/Striped blocks from CPZ.nem",
		"tests/s2disasm/art/nemesis/Stripy blocks from CPZ.nem",
		"tests/s2disasm/art/nemesis/Super Sonic stars.nem",
		"tests/s2disasm/art/nemesis/Swinging platform from OOZ.nem",
		"tests/s2disasm/art/nemesis/Tails continue.nem",
		"tests/s2disasm/art/nemesis/Tails life counter.nem",
		"tests/s2disasm/art/nemesis/Tails on continue screen.nem",
		"tests/s2disasm/art/nemesis/Tails text patterns from special stage.nem",
		"tests/s2disasm/art/nemesis/The Tornado.nem",
		"tests/s2disasm/art/nemesis/Thrust from Robotnik's getaway ship in WFZ.nem",
		"tests/s2disasm/art/nemesis/Tilting plaforms in WFZ.nem",
		"tests/s2disasm/art/nemesis/Title card.nem",
		"tests/s2disasm/art/nemesis/Top of water in ARZ.nem",
		"tests/s2disasm/art/nemesis/Top of water in HPZ and CNZ.nem",
		"tests/s2disasm/art/nemesis/Transporter ball from OOZ.nem",
		"tests/s2disasm/art/nemesis/Turtle badnik from SCZ.nem",
		"tests/s2disasm/art/nemesis/Turtle.nem",
		"tests/s2disasm/art/nemesis/Unused badnik from WFZ.nem",
		"tests/s2disasm/art/nemesis/Unused vertical laser in WFZ.nem",
		"tests/s2disasm/art/nemesis/Vertical impulse spring.nem",
		"tests/s2disasm/art/nemesis/Vertical shadow from special stage.nem",
		"tests/s2disasm/art/nemesis/Vertical spinning blades in WFZ.nem",
		"tests/s2disasm/art/nemesis/Vertical spring.nem",
		"tests/s2disasm/art/nemesis/Vine that lowers from MCZ.nem",
		"tests/s2disasm/art/nemesis/WFZ boss chamber switch.nem",
		"tests/s2disasm/art/nemesis/WFZ boss.nem",
		"tests/s2disasm/art/nemesis/Wall turret from WFZ.nem",
		"tests/s2disasm/art/nemesis/Waterfall tiles.nem",
		"tests/s2disasm/art/nemesis/Weird crawling badnik from CPZ.nem",
		"tests/s2disasm/art/nemesis/Wheel for belt in WFZ.nem",
		"tests/s2disasm/art/nemesis/Window in back that Robotnik looks through in DEZ.nem",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Lap Numbers.bin",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Misc Art 1.bin",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Misc Art 2.bin",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Misc Art 3.bin",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Position Icons.bin",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Spindash Dust.bin",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Start Post.bin",
		"tests/skdisasm/General/2P Zone/Nemesis Art/Time Display.bin",
		"tests/skdisasm/General/Blue Sphere/Nemesis Art/SK Logo.bin",
		"tests/skdisasm/General/Blue Sphere/Nemesis Art/Tails Pose.bin",
		"tests/skdisasm/General/Ending/Nemesis Art/Knuckles Ending Pose.bin",
		"tests/skdisasm/General/Ending/Nemesis Art/Large Text.bin",
		"tests/skdisasm/General/Ending/Nemesis Art/S3 8x16 Font.bin",
		"tests/skdisasm/General/Ending/Nemesis Art/S3 Ending Graphics.bin",
		"tests/skdisasm/General/Ending/Nemesis Art/S3 Large Text.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/1P 2P Wins.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/2P Options.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/Level Select Icons.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/Menu Box.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/Signpost.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/Sonic Continue Icon.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/Tails Continue Icon.bin",
		"tests/skdisasm/General/S2Menu/Nemesis Art/Tails Continue Sprites.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/BG.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Digits.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Eosian Spheres.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Get Blue Spheres Arrow.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Get Blue Spheres.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Icons.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Layout.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Ring.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Shadow.bin",
		"tests/skdisasm/General/Special Stage/Nemesis Art/Sphere.bin",
		"tests/skdisasm/General/Sprites/Animals/Blue Flicky.bin",
		"tests/skdisasm/General/Sprites/Animals/Chicken.bin",
		"tests/skdisasm/General/Sprites/Animals/Penguin.bin",
		"tests/skdisasm/General/Sprites/Animals/Pig.bin",
		"tests/skdisasm/General/Sprites/Animals/Rabbit.bin",
		"tests/skdisasm/General/Sprites/Animals/Seal.bin",
		"tests/skdisasm/General/Sprites/Animals/Squirrel.bin",
		"tests/skdisasm/General/Sprites/Boss Explosion/Boss Explosion.bin",
		"tests/skdisasm/General/Sprites/Bubbles/Bubbles.bin",
		"tests/skdisasm/General/Sprites/Buggernaut/Buggernaut.bin",
		"tests/skdisasm/General/Sprites/Buttons/Gray Button.bin",
		"tests/skdisasm/General/Sprites/Continue/Player Sprites.bin",
		"tests/skdisasm/General/Sprites/Continue/Player Icons.bin",
		"tests/skdisasm/General/Sprites/Continue/Digits.bin",
		"tests/skdisasm/General/Sprites/Egg Capsule/Egg Capsule.bin",
		"tests/skdisasm/General/Sprites/Egg Robo/Egg Robo Run.bin",
		"tests/skdisasm/General/Sprites/Egg Robo/Egg Robo Stand.bin",
		"tests/skdisasm/General/Sprites/Enemy Misc/EnemyPtsStarpost.bin",
		"tests/skdisasm/General/Sprites/Enemy Misc/Explosion.bin",
		"tests/skdisasm/General/Sprites/Game Over/GameOver.bin",
		"tests/skdisasm/General/Sprites/HUD Icon/Knuckles Life Icon.bin",
		"tests/skdisasm/General/Sprites/HUD Icon/Miles Life Icon.bin",
		"tests/skdisasm/General/Sprites/HUD Icon/Sonic Life Icon.bin",
		"tests/skdisasm/General/Sprites/HUD Icon/Tails Life Icon.bin",
		"tests/skdisasm/General/Sprites/Knuckles/Cutscene/Knuckles Bomb.bin",
		"tests/skdisasm/General/Sprites/Level Misc/Diagonal Spring.bin",
		"tests/skdisasm/General/Sprites/Level Misc/SpikesSprings.bin",
		"tests/skdisasm/General/Sprites/Monitors/Monitors.bin",
		"tests/skdisasm/General/Sprites/Ring/RingHUDText.bin",
		"tests/skdisasm/General/Sprites/Robotnik/FBZ Robotnik Head.bin",
		"tests/skdisasm/General/Sprites/Robotnik/FBZ Robotnik Run.bin",
		"tests/skdisasm/General/Sprites/Robotnik/FBZ Robotnik Stand.bin",
		"tests/skdisasm/General/Sprites/Robotnik/Ship.bin",
		"tests/skdisasm/General/Sprites/Signpost/Stub.bin",
		"tests/skdisasm/General/Sprites/Snowboard/Snowboard Dust.bin",
		"tests/skdisasm/General/Title/Nemesis Art/S3 Banner.bin",
		"tests/skdisasm/General/Title/Nemesis Art/S3 Screen Text.bin",
		"tests/skdisasm/General/Title/Nemesis Art/S3 Sonic Sprites.bin",
		"tests/skdisasm/General/Title/Nemesis Art/SK ANDKnuckles.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/BG Tree.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Cork Floor 1.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Cork Floor 2.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Falling Log.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Intro Waves.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Miniboss Fire.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Miniboss Small.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Miniboss.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Misc Art 1.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Misc Art 2.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Swing Vine.bin",
		"tests/skdisasm/Levels/AIZ/Nemesis Art/Zip Vine.bin",
		"tests/skdisasm/Levels/BPZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/CGZ/Nemesis Art/Platform.bin",
		"tests/skdisasm/Levels/CNZ/Nemesis Art/End Boss.bin",
		"tests/skdisasm/Levels/CNZ/Nemesis Art/Miniboss.bin",
		"tests/skdisasm/Levels/CNZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/CNZ/Nemesis Art/Platform.bin",
		"tests/skdisasm/Levels/DEZ/Nemesis Art/Act 2 Extra Art.bin",
		"tests/skdisasm/Levels/DEZ/Nemesis Art/Miniboss.bin",
		"tests/skdisasm/Levels/DEZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/DPZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/EMZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/Act 2 Subboss.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/Egg Capsule.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/End Boss Flame.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/End Boss.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/Misc Art 1.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/Misc Art 2.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/Outdoors.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/S3 Miniboss.bin",
		"tests/skdisasm/Levels/Gumball/Nemesis Art/Gumball Bonus.bin",
		"tests/skdisasm/Levels/FBZ/Nemesis Art/Outdoors.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Act 2 Block Platform.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Act 2 Knuckles Wall.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Act 2 Slide.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Button.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/End Boss.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Miniboss.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Spike Ball.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Water Rush.bin",
		"tests/skdisasm/Levels/HCZ/Nemesis Art/Wave Splash.bin",
		"tests/skdisasm/Levels/HPZ/Nemesis Art/Emerald Misc Art.bin",
		"tests/skdisasm/Levels/HPZ/Nemesis Art/Gray Emerald.bin",
		"tests/skdisasm/Levels/ICZ/Nemesis Art/End Boss.bin",
		"tests/skdisasm/Levels/ICZ/Nemesis Art/Intro Sprites.bin",
		"tests/skdisasm/Levels/ICZ/Nemesis Art/Miniboss.bin",
		"tests/skdisasm/Levels/ICZ/Nemesis Art/Misc Art 1.bin",
		"tests/skdisasm/Levels/ICZ/Nemesis Art/Misc Art 2.bin",
		"tests/skdisasm/Levels/ICZ/Nemesis Art/Teleporter Beam.bin",
		"tests/skdisasm/Levels/LBZ/Nemesis Art/Act 2 Misc Art.bin",
		"tests/skdisasm/Levels/LBZ/Nemesis Art/Final Boss 1.bin",
		"tests/skdisasm/Levels/LBZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/LBZ/Nemesis Art/Tube Transport.bin",
		"tests/skdisasm/Levels/LRZ/Nemesis Art/Act 2 Misc Art.bin",
		"tests/skdisasm/Levels/LRZ/Nemesis Art/Act 2 Spinning Drum.bin",
		"tests/skdisasm/Levels/LRZ/Nemesis Art/Big Spike Ball.bin",
		"tests/skdisasm/Levels/LRZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/LRZ/Nemesis Art/Spike Crush.bin",
		"tests/skdisasm/Levels/MGZ/Nemesis Art/Direction Signs.bin",
		"tests/skdisasm/Levels/MGZ/Nemesis Art/Misc Art 1.bin",
		"tests/skdisasm/Levels/MGZ/Nemesis Art/Misc Art 2.bin",
		"tests/skdisasm/Levels/MGZ/Nemesis Art/Spire.bin",
		"tests/skdisasm/Levels/MHZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/Pachinko/Nemesis Art/Main.bin",
		"tests/skdisasm/Levels/Slots/Nemesis Art/Blocks.bin",
		"tests/skdisasm/Levels/SOZ/Nemesis Art/Act 2 Extra Art.bin",
		"tests/skdisasm/Levels/SOZ/Nemesis Art/Misc Art.bin",
		"tests/skdisasm/Levels/SOZ/Nemesis Art/Tile.bin",
		"tests/skdisasm/Levels/SSZ/Nemesis Art/Misc.bin",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Aquis.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Arrow_S.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Asteron.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/AutoDoor.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BBumpers.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BMonster.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/B_Blades.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Backgnd.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Balkiry.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Ball.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Batbot.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Bear.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Blink.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BlueBird.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Boost_Up.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Boss.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BossBall.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BossFire.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Boss_Car.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Boss_Smk.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Box.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Bridge.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BrkBlock.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BrkBst_H.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/BrkBst_V.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Bubbles.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Bumpers.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Buzzer.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/CNZDyn_Init.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/CPZBoss.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/CPZDyn_Init.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/CPZPlatform.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Cannon.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Chicken.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/ChopChop.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Clp_Ptfm.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Clucker.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Coconuts.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Crawl.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Crawlton.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Crocobot.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/DHZBoss.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/DHZBox.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/DSpring1.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/D_Launch.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/D_Spring.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Dinobot.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/DynInit2.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Dyn_Init.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/EHZBridge.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/EHZWatrFall.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Elevator.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Emerald.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/EndPanel.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Explosn.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Explosns.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Fans.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Fire_Bst.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Fireball.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Flasher.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Flippers.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/GBumpers.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/GSpkball.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/GT_Over.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Gear.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/GearHole.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Grabber.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/GreenPtf.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Grounder.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/HPZBridge.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/HPZWatrFall.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/HTZAutoDoor.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/HTZDyn_Init.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/H_Spikes.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/H_Spring.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Harp_Ptf.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Harpoon.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Hud.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/HudSonic.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/InvStars.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/LampPost.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Lander.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Leaves.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/LvBubble.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/MZDyn_Init.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/MZElevator.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/MZLvBubble.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Masher.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Metal_St.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/MiniGear.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Miscelns.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Monitors.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Monkey.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Motobug.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Mouse.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/NGHZAutoDoor.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/NGHZBoss.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/NGHZDyn_Init.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/NGHZWatrSurf.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Nebula.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/OC_Ptfrm.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/OOZBoss.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/OOZDyn_Init.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/OOZElevator.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/OOZPlatform.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Octus.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Oil.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Oil_01.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Orbs.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Oxygen.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Parallel.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Penguin.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Pig.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Pigeon.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Piranha.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Platform.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Points.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Rabbit.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Rexon.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Rhinobot.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Rings.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Robotnik.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Rock.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/ScrewNut.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Seal.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/See-saw.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/See-sawb.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Sega.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Shellcrc.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Shield.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/ShpBoost.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Slicer.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/SlotMach.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/SncMlScr.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/SpeedBst.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/SpgTubes.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Spikball.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Spiker.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Spikes.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/SpinBall.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/SpngPush.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Squirrel.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Steam.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Switch.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/SwngPtfm.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Telefrcs.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/TitleScr.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/TlpFlash.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Tri_Ptfm.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Turtle.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Turtloid.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/UnkFball.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/UnkPtfm.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/V_Launch.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/V_Spring.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Vines.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Vines_1.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/W_Splash.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/WatrSurf.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Whisp.nem",
		"tests/Sonic-2-Aug-21st-Disassembly/Art/Nemesis/Worms.nem",
	};

	total_uncompressed_size = total_original_compressed_size = total_new_compressed_size = 0;

	MemoryStream_Initialise(&compressed_memory_stream);
	MemoryStream_Initialise(&decompressed_memory_stream);
	MemoryStream_Initialise(&compressed_memory_stream_2);
	MemoryStream_Initialise(&decompressed_memory_stream_2);

	for (i = 0; i < CC_COUNT_OF(files); ++i)
	{
		const char* const file_path = files[i];
		FILE* const file = fopen(file_path, "rb");

		if (file == NULL)
		{
			fprintf(stdout, "Could not open file '%s' for reading.\n", file_path);
		}
		else
		{
			MemoryStream_Clear(&compressed_memory_stream);
			MemoryStream_Clear(&decompressed_memory_stream);
			MemoryStream_Clear(&compressed_memory_stream_2);
			MemoryStream_Clear(&decompressed_memory_stream_2);

			/* TODO: Make this less gross. */
			for (;;)
			{
				const int byte = ReadByteFromFile(file);

				if (byte == CLOWNNEMESIS_EOF)
					break;
				else if (byte == CLOWNNEMESIS_ERROR)
					fprintf(stdout, "Could not read file '%s'.\n", file_path);
				else
					WriteByteToMemoryStream(&compressed_memory_stream, byte);
			}

			fclose(file);

			if (!ClownNemesis_Decompress(ReadByteFromMemoryStream, &compressed_memory_stream, WriteByteToMemoryStream, &decompressed_memory_stream))
			{
				fprintf(stdout, "Could not decompress file '%s'.\n", file_path);
			}
			else
			{
				if (!ClownNemesis_Compress(accurate, ReadByteFromMemoryStream, &decompressed_memory_stream, WriteByteToMemoryStream, &compressed_memory_stream_2))
				{
					fprintf(stdout, "Could not compress file '%s'.\n", file_path);
				}
				else
				{
					if (!ClownNemesis_Decompress(ReadByteFromMemoryStream, &compressed_memory_stream_2, WriteByteToMemoryStream, &decompressed_memory_stream_2))
					{
						fprintf(stdout, "Could not re-decompress file '%s'.\n", file_path);
					}
					else
					{
						if (decompressed_memory_stream.write_index != decompressed_memory_stream_2.write_index || memcmp(decompressed_memory_stream.buffer, decompressed_memory_stream_2.buffer, decompressed_memory_stream.write_index) != 0)
						{
							fprintf(stdout, "Decompressions of file '%s' do not match.\n", file_path);
						}
						else
						{
							total_original_compressed_size += compressed_memory_stream.write_index;
							total_uncompressed_size += decompressed_memory_stream.write_index;
							total_new_compressed_size += compressed_memory_stream_2.write_index;

							if (accurate)
							{
								if ((compressed_memory_stream.write_index < compressed_memory_stream_2.write_index || memcmp(compressed_memory_stream.buffer, compressed_memory_stream_2.buffer, compressed_memory_stream_2.write_index) != 0))
									fprintf(stdout, "Compressions of file '%s' do not match.\n", file_path);
								else if (compressed_memory_stream.write_index > compressed_memory_stream_2.write_index)

								{
									FILE* file = fopen(file_path, "wb");
									fwrite(compressed_memory_stream_2.buffer, compressed_memory_stream_2.write_index, 1, file);
									fclose(file);

									fprintf(stdout, "File '%s' has %ld bytes of junk data at the end.\n", file_path, (unsigned long)(compressed_memory_stream.write_index - compressed_memory_stream_2.write_index));
								}
							}
						}
					}
				}
			}
		}
	}

	MemoryStream_Deinitialise(&compressed_memory_stream);
	MemoryStream_Deinitialise(&decompressed_memory_stream);
	MemoryStream_Deinitialise(&compressed_memory_stream_2);
	MemoryStream_Deinitialise(&decompressed_memory_stream_2);

	fprintf(stdout, "Uncompressed size:   %ld\nOld compressed size: %ld\nNew compressed size: %ld\nNew vs. old: %f%%\n", (unsigned long)total_uncompressed_size, (unsigned long)total_original_compressed_size, (unsigned long)total_new_compressed_size, (double)total_new_compressed_size / total_original_compressed_size * 100);
}

int main(const int argc, char** const argv)
{
	(void)argc;
	(void)argv;

	fputs("Testing accurate compression...\n", stdout);
	DoTests(cc_true);
	fputs("\nTesting improved compression...\n", stdout);
	DoTests(cc_false);

	return EXIT_SUCCESS;
}
