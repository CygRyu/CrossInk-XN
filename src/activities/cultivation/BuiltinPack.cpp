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
An outer elder of the {sect.name} fines you two taels for practising on the roof of the scripture hall.
You guard a salt caravan for a month and come back with a purse of silver and a scar on your forearm.
A senior disciple challenges you in the training yard, and you last [ten|twenty|thirty] exchanges before he knocks you down.
You buy a Qi-gathering pill in {place.town} that turns out to be rolled flour, and the seller has already left town.
Your Qi runs the wrong way during a night of meditation, and you cough blood for three days.
You guard a herb field for the {sect.name} and chase off {beast.a} with a burning branch.
You copy sutras for a scholar in {place.town} by day and meditate in his woodshed by night.
You sell a month of firewood to the kitchens of the {sect.name} for three taels and spend them all on a bowl of ginseng broth.
A rogue cultivator robs you on the road below {name.mountain} and leaves you your boots.
You learn to push Qi into your fists, and the first time you try it on a tree you break your wrist.
A disciple of the {sect.name} pays you to take his turn at night watch, and you meditate on the wall until dawn.
You lose a game of weiqi to an old man in {place.town} and owe him a month of chopping wood.
The {sect.name} offers ten taels for the hide of {beast.a}, and you bring back two.
You trade your only good robe for a cracked jade slip that holds half of a breathing method.
A healer in {place.town} charges you five taels to set a rib you cracked sparring and tells you to stop sparring.
Two outer disciples fight over a silver seam on {name.mountain}, and you are paid to carry the wounded down.
You spend the {$season} in a rented room above a noodle shop in {place.town}, meditating through the noise of the kitchen.
Your first try at storing Qi in your dantian leaves you dizzy for two days and owing the innkeeper for three.
A caravan master hires you to stand behind him during a deal and pays you not to say a word.
You find {herb} growing in a crack on {name.mountain} and sell it to a pill shop for half its worth because you need the money that day.
An outer elder catches you reading the posted manuals without paying the copying fee and fines you a month's stipend.
You lose a sparring match to a girl two years younger and spend the {$season} practising the step she beat you with.
Bandits take over your inn in {place.town}, and you hide in the cellar until the magistrate's men arrive.
You escort a tax wagon to {place.town} and are paid in copper so heavy you have to hire a mule to carry it home.
A scholar in {place.town} pays you to sit in his courtyard every night after his neighbour's house is robbed.
You open a new meridian in your left arm and can lift a full water cask with one hand.
The quartermaster of the {sect.name} charges you double for a sleeping mat because you are not a disciple.
A wandering swordsman shows you three moves of a sword form for the price of your dinner and leaves before the fourth.
You catch a thief at the market in {place.town} and find out he is the son of the man who hired you.

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
A tea-house owner in {place.town} lets you sleep on his benches in exchange for carrying water up from the well.
You help a widow in {place.town} drag her husband's boat out of the river and stay for the funeral.
A stranger in {place.town} takes you for the cousin of a man who owes him money, and it takes you a week to prove him wrong.
You lose your purse in the crowd at the lantern festival in {place.town} and win back half of it at an archery stall.
An early frost kills the crops near {place.town}, and the price of rice doubles before the end of the {$season}.
You carry a sick traveller to a doctor in {place.town} and end up paying for his medicine.
Two families in {place.town} hire you on the same day to settle the same quarrel, and you take money from both.
A blacksmith in {place.town} sharpens your knife for free because you once helped his brother.
Heavy rain washes out the bridge at {place.town}, and you spend three weeks hauling timber to rebuild it.
You are accused of stealing a chicken in {place.town} and spend a night in the cells before the real thief is caught.
A travelling opera troupe pays you to guard their costume chests on the road to {place.town}.
You share a fire with a salt smuggler in {place.region}, and he tells you which checkpoints to avoid.
An old woman in {place.town} pays you a basket of eggs to drive {beast.a} away from her chicken run.
A merchant in {place.town} tries to pay you with a forged silver ingot, and you make him buy it back in front of the whole market.
You drink from a bad well in {place.region} and lose a week to fever.
A gambler in {place.town} bets everything on you in a wrestling match and gives you a tenth of his winnings.
Soldiers close the road through {place.region} for a month to hunt deserters, and you wait it out in {place.town}.
You help a scholar carry his books to {place.town}, and he pays you with a book of maps.
You spend the {$season} mending roofs in {place.town} after a storm and come away with a full purse and a twisted knee.
A man you beat in a fight years ago turns up in {place.town} as the magistrate's new constable.

[log.foundation]
The {sect.name} makes you an inner disciple and gives you a room with a door that locks.
You lead [five|eight|ten] outer disciples to clear {beast.a} out of a mine on {name.mountain} and bring all of them back.
An elder of the {sect.name} asks you to tutor his grandson, who falls asleep in every lesson.
You fly on a sword for the first time, crash into a pine tree and walk the rest of the way.
A rival inner disciple accuses you of stealing a pill from the alchemy hall, and the steward's ledger clears your name.
You spend the {$season} sealed in a cave on {name.mountain}, coming out only to buy rice.
The {sect.name} sends you to collect a debt from a merchant in {place.town}, and he pays the moment he sees your robe.
You win a sparring tournament between inner disciples and take home [twenty|thirty|fifty] taels and a bruised jaw.
A demonic cultivator kills two disciples on the road to {place.town}, and you are sent to bury them.
You buy a plot of spirit fields below {name.mountain} and lose the first harvest to locusts.
An old enemy from your mortal days comes to the sect gate asking for work, and you give it to him.
You settle a border quarrel between the {sect.name} and a family of rogue cultivators by beating their eldest son.
The sect treasury pays your stipend late for the third month running, and you stop asking.
You escort an envoy of the {sect.name} to {place.town} and stand behind him through six hours of tea.
A junior disciple you trained breaks his leg on a mission you planned, and you pay for his medicine.
You forge a flying sword from black iron bought in {place.town}, and it bends the first time you use it.
A matchmaker from {place.town} brings a proposal from a salt merchant's family, and you send her away with tea.
You hold the pass through {place.region} for three days against bandits until the sect sends help.
An elder offers to take you as his personal disciple if you marry his niece, and you ask for time to think.
You lose [a hundred|two hundred] taels in the sect's gambling den and win them back by morning.
You judge a tournament of outer disciples, and the losers' families send you gifts all {$season}.
You trade blows with a disciple of a rival sect at the market in {place.town}, and both sects agree to forget it.
A wounded beast tamer begs for shelter at your cave, and you hide him from the men hunting him.
You copy a manual for the sect library and are allowed to keep a copy for yourself.
Your first teacher from your mortal days dies in {place.town}, and you pay for the funeral.

[log.gp]
You spend the {$season} sitting in a cold spring on {name.mountain}, letting your Qi settle.
You go over the same breathing method every morning until you can run it without thinking.
You turn down a paid escort job to stay in seclusion, and your purse runs thin by the end of the {$season}.
A senior who broke through too early shows you the cracks in his foundation, and you decide to wait longer.
You practise holding your Qi at its limit for a full day without letting any of it leak.
You test your control by carving characters into stone with a fingertip until the strokes come out even.
You fast for ten days in your cave, and your Qi runs smoother for it.
A storm cuts you off on {name.mountain} for a month, and you spend it refining what you have.
You patch the small leaks in your meridians one by one, the way a fisherman mends a net.
Your seclusion is broken by a messenger with a bill from your landlord in {place.town}.

[log.halfstep]
The meridians you tore in your failed breakthrough ache whenever it rains.
You spend the {$season} going over what went wrong in your last breakthrough, one meridian at a time.
A physician in {place.town} charges you ten taels to tell you to rest.
Word of your failed breakthrough reaches {place.town}, and the innkeeper stops giving you credit.
You cough blood every morning for a month after the failed breakthrough, and then it stops.
An old cultivator in {place.town} tells you he failed four times before he broke through, and buys you a drink.
You rebuild your Qi slowly, a little every night, careful with the damaged meridians.
People in the market at {place.town} start calling you Half-step, and you stop correcting them.

[log.breakthrough]
Your Qi breaks through the barrier, and you step into {$player.realm}.
You come out of seclusion in {$player.realm}, and the first thing you do is eat three bowls of rice.
The barrier gives way after a night of sweating through your robes, and you wake in {$player.realm}.
You break through to {$player.realm}, and your old injuries stop aching for the first time in years.
You reach {$player.realm} with a crack like a snapping branch inside your chest, and then nothing hurts.

[log.failure]
Your Qi hits the barrier of {$player.realm}, turns back on you, and you spit blood onto the floor of your room.
The breakthrough fails halfway, and you lose a quarter of your Qi trying to hold it together.
You push too hard against the limit of {$player.realm}, and two of your meridians tear.
You faint in the middle of the breakthrough and wake two days later with nothing gained.
The barrier holds, and you come out of your room thinner and no stronger.

[log.gp.enter]
You decide to stay at the peak of {$player.realm} a while longer and build your foundation slowly.
You turn away from the breakthrough for now and settle in to consolidate what you have.
You tell no one you are ready to break through and keep working on what you already have.

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
