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
# Path of Immortal Reading: built-in pack, version 2

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
[Upper|Lower] [Heng|Qiu|Dai] Village

[name.mountain]
[Azure|Crane|Thunder|Cold Moon|Nine Pines|Broken Sword] Peak
Mount [Heng|Yao|Lin|Qiu]
the Sleeping Ox Ridge

# --- People and powers ---------------------------------------------------

[sect.name]
[Azure|Crimson|Jade|Violet|Golden] [Cloud|Lotus|Crane|Sword|Moon] [Sect|Pavilion|Valley|Palace]

[cultivator.name]
[Old|Young] [Bai|Mo|Su|Gu|Feng|Lan|Zhou|Hu]
Butcher [Zhang|Wang|Tie]
Auntie [Ma|Qin|Lu]
the one-armed swordsman [Gu|Ren|Shi]
Third Brother [Qian|Tang|Wei]
Debt Collector [Pan|Luo]

[realm.any]
Foundation Establishment
Core Formation
Nascent Soul

# --- Nature ---------------------------------------------------------------

[beast]
tiger
[white|black] ape
grey wolf
rock python
brown bear
leopard
wild boar

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
# Tea-house talk: who did what to whom, where, and what it cost.

[rumor]
{rumor.opening} {rumor.body}.

[rumor.opening]
Travellers on the post road say
In the tea-houses of {place.town} they say
A ferryman at {place.town} swears
Salt merchants back from {place.region} claim
The porters at {place.town} say
An outer disciple of the {sect.name} let slip that
A drunk escort guard told the whole inn that
Word from {place.region} is that

[rumor.body]
the {sect.name} is paying [twenty|fifty|a hundred] taels a head for anyone who saw who robbed its convoy
{cultivator.name} lost two fingers to {beast.a} in {place.region} and still won't say what the trip was for
the ferry at {place.town} stopped running after the boatman was found with a sword wound in his back
the {sect.name} and the [Iron Fist|Black Water|River Wolf] Gang both claim the [iron|copper|jade] mine on {name.mountain}
a disciple of the {sect.name} pawned his master's sword in {place.town} for [eight|twelve|thirty] taels and ran
the {herb} harvest failed this [spring|autumn], and it sells for [ten|twenty|forty] Spirit Stones a bundle in {place.town}
{beast.a} took [two|three|four] mules off the road near {place.town}, and the magistrate has posted a reward
the {sect.name} threw out an elder for selling its entrance tokens to rich merchants' sons
the herbalist of {place.town} saved the magistrate's son and was paid in debt notes no money-house will take
an elder of the {sect.name} broke through to {realm.any} at [sixty|eighty|ninety] and fed the whole town for three days
{cultivator.name} owes the gambling house in {place.town} [three hundred|five hundred|a thousand] taels and has left town
bandits on {name.mountain} let travellers pass for one Spirit Stone each and beat anyone who pays in copper
the {sect.name} will test new disciples in {place.town} after the [spring|autumn] harvest, and every inn is already full
a landslide on {name.mountain} opened an old tomb, and [two|three] diggers who went in came out coughing blood
{cultivator.name} broke the sword arm of a disciple of the {sect.name} in a duel at {place.town}
the salt tax in {place.town} doubled after the magistrate's nephew joined the {sect.name}
the [wolves|wild boars|black apes] in {place.region} have grown so bold that the post riders only travel by day
the smith in {place.town} forged a saber for the {sect.name} and was paid in Spirit Stones
a girl from {place.town} knelt at the gate of the {sect.name} for [three|five|seven] days and was taken on in the kitchens
thieves dug under the treasury of the {sect.name} and took nothing but one box of pills
[four|six|nine] men have died this {season} looking for a sword manual in a ruined shrine on {name.mountain}
lightning burned the granary at {place.town}, and the {sect.name} sent rice instead of silver
a stone fell from the sky near {place.town}, and smiths are paying its weight in silver for pieces of it
the {sect.name} closed the pass through {place.region} and charges [five|ten] taels to anyone who wants to use it
{cultivator.name} sold fake {herb} to the {sect.name} and has not been seen since
a caravan lost [six|ten|twenty] men to {beast.a} in {place.region}, and the survivors are hiring guards at double pay

[rumor@1]
{rumor.opening} a floating island broke its anchor chains and drifted [three|five] days west before two Earth Immortals hauled it back.
{rumor.opening} a swordsman who reached the Immortal Plane still owes three sects for the pills that got him there.
{rumor.opening} the gatekeepers of the Immortal Plane's southern stair now charge newcomers a hundred Spirit Stones each.

# --- Life log (one line per in-game season; see tools/cultivation/PACKS.md) --
# Second person, one full sentence, under 170 bytes. {$season} and
# {$player.age} are the season the line describes. Lines grant nothing.

[log.body]
You spend the {$season} carrying stones for a mason in {place.town} and earn forty coppers and a cracked rib.
A wandering boxer in {place.town} teaches you to take a punch on the breath, for the price of a jar of wine.
You hold the horse stance every morning until you can last while a whole stick of incense burns down.
You break two knuckles on a sandbag filled with river gravel and train with your other hand for a month.
You win a wrestling match at a fair in {place.town} and lose the prize money to a pickpocket the same night.
A woodcutter shows you how to split oak along the grain, and your strikes stop jarring your elbows.
You run the hill road to {place.town} and back each dawn with a sack of rice on your shoulders.
Bandits stop you on the road to {place.town}, and you give up your coat to keep your teeth.
You sleep in a temple outside {place.town} and pay the monks by sweeping the courtyard.
A sect recruiter in {place.town} squeezes your arm, shakes his head and tells you to come back next year.
You learn to soak your hands in vinegar and salt after striking the post, and the skin stops splitting.
A fever keeps you in bed for most of the {$season}, and your landlord adds the medicine to your rent.
You trade a week of labour for a book of breathing exercises with half its pages torn out.
You fight a drunk in {place.town} over a spilled bowl of noodles and pay the innkeeper for the broken bench.

[log.body.farmer]
You dig an irrigation ditch for the village headman, and he pays you in rice.
An ox kicks you in the chest at the ploughing, and you are back in the field by evening.
You carry two full buckets to the far fields each morning and stop spilling any by the end of the month.
Your mother sells a pig to buy you a pair of boots for the road to the sects.
The headman's son laughs at your training until you lift his cart out of the mud alone.

[log.body.clan]
An old retainer of your family teaches you the first form of the clan spear, the only one he still remembers.
Creditors take the last of the furniture, and you practise in an empty hall.
You find your grandfather's seal on a debt note in a pawnshop in {place.town} and buy it back with a month of wages.
A cousin who married into the {sect.name} pretends not to know you in the market at {place.town}.

[log.body.orphan]
You run messages for a gambling house in {place.town} and learn which streets to stay out of after dark.
You steal a steamed bun, get caught, and work off the debt in the baker's kitchen for a month.
An old beggar in {place.town} teaches you to fall without breaking anything, for half of your dinner each night.
A gang from the docks takes your sleeping place under the bridge, and you take it back with a stick.

[log.body.servant]
You haul water up [three hundred|five hundred] steps to the outer sect kitchens before the bell for morning practice.
An outer disciple makes you polish his sword every night, and you study how he holds it.
You sweep the training yard after the disciples leave and copy their footwork in the dust.
The kitchen steward docks your rice for a broken jar, and you go hungry for three days.

[log.body.herbalist]
You grind {herb} for your master from dawn to dusk and burn your fingers on the drying racks.
Your master sends you up {name.mountain} for {herb}, and you come back with it and a twisted ankle.
A merchant from {place.town} pays your master in bad silver, and you are sent to collect the rest.
Your master makes you taste spirit grass and goat weed until you can tell them apart by smell.

[log.qi]
You sit through the {$season} in a cave on {name.mountain} and run your Qi around the small circuit [nine|twelve|thirty] times a day.
An outer elder of the {sect.name} fines you two Spirit Stones for practising on the roof of the scripture hall.
You guard a salt caravan for a month and come back with a pouch of Spirit Stones and a scar on your forearm.
A senior disciple challenges you in the training yard, and you last [ten|twenty|thirty] exchanges before he knocks you down.
You buy a Qi-gathering pill in {place.town} that turns out to be rolled flour, and the seller has already left town.
Your Qi runs the wrong way during a night of meditation, and you cough blood for three days.
You guard a herb field for the {sect.name} and chase off {beast.a} with a burning branch.
You copy sutras for a scholar in {place.town} by day and meditate in his woodshed by night.

[log.any]
A peddler from {place.town} sells you a map of {place.region} that turns out to be [mostly|about half] right.
You pull a drowning boy out of the river at {place.town}, and his father gives you a string of copper cash.
You lend [three|five] taels to a friend in {place.town} and see neither the money nor the friend again.
You help bury the dead after {beast.a} raids a farm near {place.town}.
A fortune teller in {place.town} takes your last coin and tells you to stay away from water.
Floods close the roads, and you spend the {$season} mending nets in a fishing village.
You witness a land dispute at the magistrate's court in {place.town}, and both families stop speaking to you.
You win [ten|twenty] taels at dice in {place.town} and lose them again before morning.
Soldiers press you into hauling grain for the county garrison for a month without pay.
You hear that {cultivator.name} is asking after you over an old debt, and you take the long road around {place.town}.

[log.stage]
Your Qi fills out, and you reach {$player.realm}, {$player.stage}.
After [a week|ten days|a month] of aching joints, you reach {$player.realm}, {$player.stage}.
You reach {$player.realm}, {$player.stage}, and your breathing stays even through a full day's work.

[log.bottleneck]
Your Qi presses against the limit of {$player.realm} and goes no further, however long you sit.
You reach the peak of {$player.realm}, and you will need a breakthrough to go further.

# --- The Chronicle's opening (one chapter per life) -----------------------

[chronicle.birth.farmer]
You were born in a village at the foot of the hills, the [second|third|fourth] child of a family that farmed rented land.

[chronicle.birth.clan]
You were born into the last branch of a fallen clan, in a house with more creditors than servants.

[chronicle.birth.orphan]
You were left in the alleys of a market town as a baby, and a noodle seller fed you until you could beg for yourself.

[chronicle.birth.servant]
You were raised hauling water for an outer sect, in a room you shared with five other servants.

[chronicle.birth.herbalist]
You were raised among drying racks and mortars by an herbalist who took you in to grind roots for him.

[chronicle.awakening]
At sixteen you pressed your palm to the elder's testing stone. It glowed a {$root.glow} {$root.color}, and he wrote "{$player.element} root" in his register.
At sixteen the testing elder held your hand to the stone until it glowed a {$root.glow} {$root.color}, then wrote "{$player.element} root" in his register.

[chronicle.past]
In a former life you turned {$past.pages} and finished {$past.books}.

# --- Sample event (exploration arrives in a later milestone) ---------------

[ruin.desc]
a [collapsed|half-buried|burned-out] shrine
the gate of a sect wiped out [forty|sixty|a hundred] years ago
a robbers' storeroom dug into the hillside
an abandoned border fort with its well filled in

[ruin.loot]
Under a loose floor slab you find a cloth purse of Spirit Stones.
A lacquered box holds a few pages of a sword manual; the rest were torn out.
A dead man's pack still holds a pouch of Spirit Stones.

[ruin.trap]
A rotten beam gives way and you twist your ankle on the way down.
Someone rigged the doorway with a crossbow, and the bolt grazes your arm.
An old formation flares up and burns your sleeve.

[event.ruins]
@weight 6
@req realm>=1 stamina>=1
While crossing {place.region>where}, you find {ruin.desc}.
> Search the ruins carefully
  @check luck 40
  @win stones+[20-60] text={ruin.loot}
  @lose hp-10 text={ruin.trap}
> Burn incense for the dead and move on
  @effect karma+3 text=You burn a stick of incense for the dead of {$where} and go on your way.
)PACK";

}  // namespace

const char* builtinPackText() { return kBuiltinPack; }
size_t builtinPackSize() { return sizeof(kBuiltinPack) - 1; }

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
