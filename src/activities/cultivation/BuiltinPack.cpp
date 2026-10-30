#include "CultivationConfig.h"

#if CROSSINK_CULTIVATION

#include "BuiltinPack.h"

namespace cultivation {
namespace {

// The built-in content pack, in flash. Same format as SD packs (see
// tools/cultivation/PACKS.md); SD packs are layered on top of it. All text is
// original to this project. Keep lines under 191 bytes; the host tests check
// this pack on every CI run.
static const char kBuiltinPack[] = R"PACK(
# Path of Immortal Reading: built-in pack, version 1

# --- Places --------------------------------------------------------------

[place.region]
the Mistveil Marshes
the Ember Wastes
the Valley of [Bleached|Silent|Hungry] Bones
the [Jade|Ashen|Sunken] Forest
the Thousand-Step Cliffs
the Drifting Salt Flats
the Ninefold River Delta

[place.town]
[Willow|Lantern|Stone|Reed|Cinder|Pine] [Crossing|Market|Ford|Hollow|Bridge]
Lanshui Town
the market town of Baiyun
the ferry village under {name.mountain}

[name.mountain]
[Azure|Crane|Thunder|Cold Moon|Nine Pines|Broken Sword] Peak
Mount [Heng|Yao|Lin|Qiu]
the Sleeping Ox Ridge

# --- People and powers ---------------------------------------------------

[sect.name]
[Azure|Crimson|Jade|Violet|Golden] [Cloud|Lotus|Crane|Sword|Moon] [Sect|Pavilion|Valley|Palace]

[cultivator.name]
[Old|Young|Elder] [Bai|Mo|Su|Gu|Feng|Lan]
the [one-armed|blind|laughing|silent] swordsman
a [nameless|grey-robed|barefoot] wanderer

[realm.any]
Foundation Establishment
Core Formation
Nascent Soul

# --- Nature ---------------------------------------------------------------

[beast]
fox
[white|black] ape
ox
serpent
crane
tortoise
qilin
wolf
owl

[herb]
spirit grass
[red|blue] lotus seed
moon orchid
thunder root
frost ginseng

[season]
spring
summer
autumn
winter

[time.day]
dawn
dusk
midnight

[number.small]
three
seven
nine

# --- Rumors (one per day on the Cultivator screen) ------------------------

[rumor]
{rumor.opening} {rumor.body}.

[rumor.opening]
Travellers whisper that
In the tea-houses they say
A ferryman swears that
Merchants from {place.town} claim that
Outer disciples gossip that
Word from {place.region} is that

[rumor.body]
a star fell over {place.region} and left a sword standing in its crater
the {sect.name} is quietly buying every {herb} in {place.town}
{beast.a} with {number.small} tails was seen drinking from a river near {place.town}
an old cultivator in {place.town} sells maps that no one can read
the mists over {place.region} have turned [silver|crimson|black] for {number.small} nights
a disciple of the {sect.name} broke through to {realm.any} after one night of meditation
a hidden cave opened on {name.mountain} after the last thunderstorm
the {sect.name} will open its gates to new disciples when {season} ends
{beast.a} has made its den in the ruins outside {place.town}
someone in {place.town} read the same book for [forty|ninety|a hundred] days without rest
a merchant from {place.region} trades pills for stories, not stones
the river below {name.mountain} runs backwards at {time.day}
the ink in an old library of the {sect.name} moves when no one is watching
{cultivator.name} asks every traveller the same riddle and has not heard the right answer yet
lightning struck {name.mountain} [twice|three times|nine times] on a cloudless day
{cultivator.name} lost a duel on {name.mountain} and has not spoken since
a library hidden under {place.town} holds a manual no two readers describe the same way
the {herb} near {place.town} bloomed out of season, all on the same night

[rumor@1]
{rumor.opening} a mortal reached the Immortal Plane by reading, not fighting.
{rumor.opening} a floating island drifted below the clouds and mortals saw its gardens.

# --- Sample event (exploration arrives in a later milestone) ---------------

[ruin.desc]
a [collapsed|half-buried|moss-eaten] shrine
the [cracked|scorched] gate of a forgotten sect
a stone stairway that ends in empty air

[ruin.loot]
Under a loose slab you find a pouch of Spirit Stones.
A faded scroll case still holds a few readable pages.

[ruin.trap]
A rusted formation flickers to life and burns your sleeve.
The floor gives way and you climb out bruised.

[event.ruins]
@weight 6
@req realm>=1 stamina>=1
While crossing {place.region>where}, you find {ruin.desc}.
> Search the ruins carefully
  @check luck 40
  @win stones+[20-60] text={ruin.loot}
  @lose hp-10 text={ruin.trap}
> Leave an offering and move on
  @effect karma+3 text=You bow to whoever once lived in {$where}.
)PACK";

}  // namespace

const char* builtinPackText() { return kBuiltinPack; }
size_t builtinPackSize() { return sizeof(kBuiltinPack) - 1; }

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
