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

# v0.2.9: rumors of your own realm, one day in three (realm 4 also serves the later realms).
[rumor.realm1]
an outer disciple of the {sect.name} reached the ninth layer in one year and was poisoned before the test
the {sect.name} charges [ten|twenty] Spirit Stones to sit in its meditation hall for a day, and the waiting list runs to autumn
a boy of the fourth layer from {place.town} challenged {cultivator.name} in the market and lost three teeth
the {sect.name} now asks for the fourth layer at its entrance test, and its recruiters take bribes
{cultivator.name} has been stuck at the ninth layer for [eleven|twenty|thirty] years and now sells talismans
a farmhand in {place.town} condensed Qi while carrying water, and two sects have sent men for him
the pill shop in {place.town} cuts its Qi pills with flour, and two disciples have already coughed blood over it
the {sect.name} lost [five|seven] outer disciples to {beast.a} in {place.region} on a herb-gathering run

[rumor.realm2]
{cultivator.name} built a cracked foundation and has been drinking in {place.town} ever since
a Foundation-Building pill sold for [eight hundred|a thousand] Spirit Stones in {place.town}, and the buyer was robbed on the way home
the {sect.name} keeps a Foundation Establishment elder chained in its cellar for selling its manuals
two Foundation cultivators fought over one spirit herb on {name.mountain} and flattened a farmer's barley field doing it
the governor wants a son-in-law of Foundation Establishment, and the {sect.name} has sent him [three|five] to choose from
a Foundation cultivator is the new magistrate of {place.town}, and the bandits have moved two valleys over
the {sect.name} executed an inner disciple for stealing a Foundation-Building pill from the elder who raised him
caravans pay a Foundation cultivator [thirty|fifty] Spirit Stones to fly ahead and look over {place.region} for beasts

[rumor.realm3]
{cultivator.name} formed a ninth-grade golden core, and the {sect.name} gave him a mountain of his own all the same
lightning struck {name.mountain} for three days while someone formed a core, and the woodcutters want compensation
a Core Formation elder of the {sect.name} died in seclusion, and his disciples have been fighting over his cave for a month
the {sect.name} hired a Core cultivator to kill {beast.a} in {place.region} and paid him with a valley
a rogue Core cultivator has taken a salt well near {place.town} and collects a tax of his own from the county
the {sect.name} lost its golden-core elder to his tribulation, and three sects have already sent marriage offers to his widow
someone cut the golden core out of a dead cultivator in {place.region}, and pill-makers are bidding for it
{cultivator.name} lived through his tribulation by hiding in a well, and every tea-house has heard

[rumor.realm4]
a Nascent Soul ancestor of the {sect.name} woke after [sixty|ninety] years of seclusion and found his sect had sold his cave
the {sect.name} and its old enemy signed a truce because each is afraid of the other's Nascent Soul elder
an old monster of the Nascent Soul realm has bought every {herb} in {place.region} for [three|five] years ahead
the emperor sent a cartload of jade to a Nascent Soul cultivator on {name.mountain} and got back a single peach
a Nascent Soul elder sent his soul out to fight a duel, and someone carried off his body while he was away
a Nascent Soul cultivator ended a border war by standing on the bridge at {place.town} for one whole day
the {sect.name} says its Nascent Soul ancestor is in seclusion, but its cooks say she has been dead for twenty years
an old monster took a twelve-year-old from {place.town} as a disciple and paid the parents in gold

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

# v0.2.9: Nascent Soul, and a shared list for Soul Transformation and later.
[log.nascent]
You spend the {$season} teaching the grandchildren of your first disciples, who are slower than their grandfathers were.
The {sect.name} asks you to judge two elders who stole from each other, and you fine them both the same.
A young cultivator walks a month from {place.town} to ask you one question, and you answer it in four words.
Your nascent soul leaves your body for the first time on {name.mountain}, and a fox makes off with your boots.
You settle a forty-year feud between two families of {place.town} by making their eldest sons dig a well together.
An envoy offers you a title and a hundred taels a year to guard the border, and you send him home with a pot of tea.
You outlive the last person who knew you as a child, and pay for her funeral in {place.town}.
A demonic cultivator who wronged you long ago is found dead in {place.region}, and you feel less than you expected.
You burn a box of letters from your youth and keep one.
The magistrate of {place.town} turns out to be the great-grandson of the magistrate who once jailed you for vagrancy.
You walk the old post road to {place.town} on foot, as you did at sixteen, and find the inn has become a temple.
Bandits on {name.mountain} know your face and send a cart of rice to your gate before you have said a word.

[log.late]
A dynasty falls in the lowlands, and the new emperor sends three officials to ask whether you mind.
You spend the {$season} learning the names of the towns around {name.mountain} again, since half of them have changed.
The {sect.name} has had six sect masters since you joined, and the newest one bows lower than you would like.
A river shifts its bed near {place.town}, and you move the village out of its way in one night.
Pilgrims leave rice and copper cash at the foot of your mountain, and a disciple carries it down to the poor of {place.town}.
You find a sword you lost two hundred years ago hanging on the wall of a tea-house in {place.town}, and leave it there.
You settle a war between two sects over a spirit vein in {place.region}, and both send you their worst wine as thanks.
A county in the south revises its tax rolls and finds it has listed you as a dead man for eighty years.
You come out of seclusion to find someone has planted beans across the mouth of your cave.
The great-granddaughter of an old enemy comes to kill you, and you send her home with a letter for her mother.
The last of your own disciples dies of old age, and you carry the coffin yourself.
A merchant guild in {place.town} names a bridge after you and charges two copper to cross it.

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

[log.core]
The {sect.name} makes you an elder, and the first thing the steward hands you is a list of debts.
You take three disciples of your own and send the laziest one home to {place.town} within a month.
A county magistrate in {place.town} asks you to find who poisoned his well, and you find it was his brother.
You fly from {name.mountain} to {place.town} in a single morning and walk the last mile so as not to frighten the farmers.
The {sect.name} sends you to negotiate a boundary with a rival sect, and you come back with half the valley.
A rogue cultivator of your own realm challenges you at the market in {place.town}, and you break his sword at the hilt.
You spend the {$season} in closed-door cultivation, and your disciples sell your vegetable garden while you are gone.
An old woman in {place.town} remembers you as a boy carrying water, and you sit with her for a whole afternoon.
You kill {beast.a} that has eaten [six|nine|eleven] villagers below {name.mountain} and give its hide to the headman.
The sect council argues for nine days over a disciple's punishment, and you vote to let him go home.
A merchant guild offers you [five hundred|a thousand] taels a year to sit on its board, and you refuse in front of witnesses.
You find one of your disciples stealing pills from the alchemy hall and make him pay it back by working in the kitchens.
A demonic sect burns a ferry town on the river, and you lead forty disciples to hunt them through the marshes.
You recover a stolen sword manual for the {sect.name} and get a jade token you never use.
You attend the funeral of an elder who taught you the sword, and the sect argues over his cave before he is buried.
A young cultivator from {place.town} follows you for three days asking to be your disciple, and on the fourth you agree.
You refine a flying sword from star iron, and this one does not bend.
You settle a blood feud between two clans in {place.town} by making both patriarchs eat at the same table.
You lose a duel of formations to an old woman selling dumplings and buy her whole stall out of respect.
The emperor's envoy asks the {sect.name} for a cultivator to guard the border, and the council sends you.
You spend [a month|two months|half a year] teaching outer disciples to breathe properly, and most of them still do it wrong.
A rival elder spreads word that your core is cracked, and you answer by sparring with him in front of the whole sect.
You buy back the land your family farmed when you were a child and give it to the tenants who work it.
You pull an injured disciple out of a collapsed mine on {name.mountain} and carry him back on your shoulders.
A letter from {place.town} tells you your oldest friend from the village has died of old age, and you go to the burial.

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

# --- Technique Scrolls (milestone 7) --------------------------------------
# A scroll's name is "{scroll.name} {scroll.kind.<kind>} {scroll.tier.<tier>}",
# expanded with the scroll's element as the theme (~wood ~fire ~earth ~metal
# ~water) and stable picks, so it reads the same every time it is shown.

[scroll.name]
{scroll.adj} {scroll.image}
{scroll.number} {scroll.heavens} {scroll.image}
{scroll.image}-{scroll.verb}
{scroll.color} {scroll.beast}
{scroll.place}

[scroll.adj]
Solitary
Boundless
Silent
Unyielding
Ancient
Profound
Wandering
Hidden
Falling
Still

[scroll.number]
Three
Seven
Nine
Ten Thousand

[scroll.heavens]
Heavens
Realms
Peaks
Rivers

[scroll.verb]
Severing
Devouring
Piercing
Sealing
Returning
Splitting
Shaking

[scroll.image]
{scroll.image.common}

[scroll.image.common]
Cloud
Lotus
Thunder
Celestial
Myriad Stars
Mountain
Moon
Wind

[scroll.image~water]
Moon
River
Frost
Cold Tide
Autumn Rain
{scroll.image.common}

[scroll.image~fire]
Sun
Flame
Ember
Red Dawn
Burning Cloud
{scroll.image.common}

[scroll.image~wood]
Pine
Bamboo
Willow
Spring
Green Vine
{scroll.image.common}

[scroll.image~metal]
Iron
Bronze Bell
West Wind
Autumn
Silver Frost
{scroll.image.common}

[scroll.image~earth]
Stone
Yellow Dust
Boulder
Great Plain
{scroll.image.common}

[scroll.color]
Azure
Crimson
Jade
Violet
Golden

[scroll.color~water]
Black
Azure
Dark

[scroll.color~fire]
Crimson
Vermilion
Scarlet

[scroll.color~wood]
Jade
Green
Azure

[scroll.color~metal]
White
Silver
Pale

[scroll.color~earth]
Golden
Yellow
Ochre

[scroll.beast]
Dragon
Phoenix
Qilin
Tortoise
Crane

[scroll.beast~water]
Tortoise
Serpent
Dragon
Crane

[scroll.beast~fire]
Phoenix
Bird
Lion
Qilin

[scroll.beast~wood]
Dragon
Deer
Crane
Phoenix

[scroll.beast~metal]
Tiger
Crane
Hawk
Wolf

[scroll.beast~earth]
Qilin
Ox
Dragon
Tortoise

[scroll.place]
Boundless Sea
Lone Peak
Hidden Valley
Nine Bends River

[scroll.place~water]
Boundless Sea
Still Lake
Cold Pool
Nine Bends River

[scroll.place~fire]
Burning Plain
Red Cliff
Furnace Peak
Sunset Ridge

[scroll.place~wood]
Bamboo Grove
Green Hill
Spring Valley
Pine Ridge

[scroll.place~metal]
Iron Gate
Autumn Pass
West Wind Pass
Bell Tower

[scroll.place~earth]
Yellow Plain
Stone Forest
Lone Peak
Hidden Valley

[scroll.kind.sword]
Sword
Saber
Blade

[scroll.kind.palm]
Palm
Fist

[scroll.kind.body]
Body Refining
Iron Skin
Bone Forging

[scroll.kind.movement]
Shadow Steps
Light Body
Wind Steps

[scroll.kind.mind]
Heart
Mind
Spirit Sense

[scroll.kind.manual]
Qi Cultivation
Breathing
Meridian

[scroll.tier.mortal]
Method

[scroll.tier.yellow]
Art

[scroll.tier.earth]
Scripture

[scroll.tier.heaven]
Canon

[scroll.tier.saint]
True Classic

# Chronicle lines for scrolls; {$scroll.name} is the scroll's name.

[scroll.line.new]
You finish the book by lamplight and write out what it taught you as the {$scroll.name}.
You sit with the book closed for an hour, then copy out the {$scroll.name} before you forget a word of it.
You spend the night writing out the {$scroll.name} and burn three candles down to the stub.
You pay a copyist in {place.town} two taels for a clean copy of the {$scroll.name}.
You sew your notes into a cover of oiled cloth and write the {$scroll.name} on the spine.
You buy good paper in {place.town} and spend three evenings copying out the {$scroll.name}.
You go over the book until you can recite it, then write down what it taught you as the {$scroll.name}.
You read the last chapter twice, then start a fair copy of the {$scroll.name} before breakfast.

[scroll.line.reread]
You go through the {$scroll.name} a second time and fill its margins with corrections.
Reading it again, you find the mistakes in your copy of the {$scroll.name} and put them right.
On a second reading the {$scroll.name} makes sense in places it never did before.

[scroll.line.sacred]
You finish the {$scroll.name} once more, and this time there is nothing left in it you do not understand.
Reading the {$scroll.name} to its last page again, you find you could write it out from memory, and better.

[scroll.line.sold]
You sell the {$scroll.name} to a book dealer in {place.town}, who pays without haggling.
Your shelves are full, so the {$scroll.name} goes to a dealer from {place.town}.
A travelling cultivator buys the {$scroll.name} from you and leaves before dawn.

[scroll.line.key]
The {$scroll.name} has nothing more to teach you, and you burn it after the breakthrough as the old rule says.
You break through on what the {$scroll.name} taught you, and its pages are worn too thin to read again.
You burn the {$scroll.name} in the courtyard after the breakthrough; you know it by heart and need it no longer.

# --- Tribulations and Heart Demons (v0.1.9) ---------------------------------
# trib.strike and demon.* scenes are shown on the Trial screen (no season
# label); the others are Chronicle lines. trib.burn has {$scroll.name}.

[trib.gather]
Clouds pile up over {name.mountain} for three days, and the farmers below drive their cattle indoors.
You climb {name.mountain} alone to wait for the lightning, carrying a gourd of water and nothing else.
The sky over {place.town} turns the colour of a bruise, and the shopkeepers put up their shutters at noon.
You send your disciples down to {place.town} and sit on a bare rock to wait for the storm.

[trib.strike]
A bolt as thick as a temple pillar comes down on the rock where you sit.
The lightning splits the pine beside you and sets the whole slope smoking.
The strike hits the ground first and comes up through your legs.
A white bolt lands so close that your ears ring and your eyebrows burn away.
The clouds drop a fork of lightning that hits you in three places at once.
The thunder arrives before the light, and the rock under you cracks in two.
A bolt walks down the mountainside toward you, burning a line through the grass.
The storm holds back for a long breath, then throws everything it has.

[trib.survive]
You walk down from {name.mountain} with your robe burned to rags and your hair standing on end.
The last bolt passes, and you sit in the rain for an hour before you trust your legs.
When the clouds break, the villagers below find you asleep on the scorched rock.

[trib.fail]
The lightning throws you off the rock, and a woodcutter finds you two days later in a gully.
You wake in a farmer's hut below {name.mountain} with burns down one side and no memory of the last strike.
The storm knocks you flat, and you spend a month learning to walk without a stick.

[trib.burn]
You throw the {$scroll.name} into the bolt's path, and the lightning takes the pages instead of you.
You burn the {$scroll.name} to turn the strike, and its ashes blow away down the mountain.

[demon.gather]
You seal the door of your cave and wait for the faces you have been trying to forget.
On the third night of seclusion the lamp gutters, and you are no longer alone in the room.
You tell your disciples not to open the door whatever they hear, and sit down to face yourself.

[demon.origin.farmer]
Your father stands at the edge of the rented field and asks why you left him to bring in the harvest alone.
The landlord's steward reads out your family's debts, and the sum is larger every time he starts again.
Your younger brother, still twelve, asks you to come home because the ox has died.

[demon.origin.clan]
The elders of your fallen clan kneel in a row and ask why you have not yet avenged them.
Your grandfather's sword lies broken across your knees, and the men who broke it are laughing in the doorway.
The clan's ancestral hall burns again, and this time you are standing outside holding the torch.

[demon.origin.orphan]
A child in rags steals your purse in the market of {place.town}, and when the child turns round it has your face.
You are seven again, sleeping under a cart, and the cart starts to move.
The woman who left you in the alley comes back for you, and you cannot tell whether to go with her.

[demon.origin.servant]
The outer sect steward hands you two water buckets and tells you your cultivation was a dream.
The disciples you once carried water for line the path and pour it out at your feet.
You are on your knees scrubbing the training ground, and the stain will not come out.

[demon.origin.herbalist]
Your old master holds out a bowl of medicine and asks why the patient you treated died.
The drying racks are full of herbs you cannot name, and someone outside is waiting for a cure.
A child you once gave the wrong powder sits by the mortar and says nothing at all.

[demon.family]
Your mother, as she looked the year you left home, asks why you never wrote.
Every friend you outlived sits at one long table, and there is no place set for you.
Your first teacher sits by the stove and tells you that you have wasted his years.
Someone you once promised to come back for waits on a bridge that rotted away fifty years ago.

[demon.failure]
You stand at the barrier of your last failed breakthrough, and your own voice tells you it will hold again.
The cultivators who laughed at you in {place.town} are all here, and they have not stopped laughing.
You watch yourself spit blood onto the floor of your room, over and over, once more each night.

[demon.years]
You count the years you have left, and the number goes down while you count.
Your face in the water bowl is old and grey, and it asks what all the sitting was for.
The village where you were born is a field now, and nobody there knows your name.

# v0.2.1: scenes from the cultivator's own history. {$region.name},
# {$beast.name} and {$scroll.name} are set where they apply.
[demon.history.guardian]
Everyone in {$region.name} drank to you the night you beat {$beast.name}; now they sit in silence and will not meet your eye.
You fight {$beast.name} in {$region.name} again, and this time your blows pass through like smoke.
The herb pickers of {$region.name} bring you the bones of {$beast.name} and ask what you mean to do with them.

[demon.history.uncleared]
Every path in {$region.name} you never walked is here, and each one ends at your door.
The herb pickers of {$region.name} ask why you went in halfway and left the rest of it to them.

[demon.history.ran]
Every path in {$region.name} ends at {$beast.name}, and every time you turn and run.
You are back in {$region.name}, and {$beast.name} is waiting on the path, as on the day you ran.

[demon.history.paid]
You count stones into the hand of {$beast.name} in {$region.name}, and the counting never stops.
In {$region.name} you pay {$beast.name} to let you pass, and the price doubles each time you look up.

[demon.history.burned]
The {$scroll.name} you burned to live through the lightning lies whole in your lap, and you cannot read a word of it.
The ash of the {$scroll.name} blows back in through the window and settles on your hands.

[demon.pass]
You come out of seclusion after nine days, eat a bowl of noodles and sleep for two more.
The faces fade one by one, and when the last is gone you open the door and ask for tea.

[demon.fail]
Your disciples break down the door on the fifth day and find you talking to the wall.
You come out of seclusion shaking, and for a month you will not sit alone in a dark room.

# --- Exploration (v0.1.10) -----------------------------------------------
# Region names, beasts and guardians are themed by region slot (~marsh ...
# ~ridge); the plain lists are the fallback. {$region.name} and
# {$beast.name} are set by the exploration screens.

[region.name]
the [Grey|Long|Broken] Hills
the [Old|Red|Stone] Valley

[region.name~marsh]
the Mistveil Marshes
the [Reedwater|Black Mud|Heron] Marshes
the [Sunken Ferry|Leech Pond|Drowned Mill] Fens

[region.name~wastes]
the Ember Wastes
the [Cinder|Scorched|Red Dust] Wastes
the [Ash|Dry Well] Plain

[region.name~forest]
the [Jade|Ashen|Old Pine] Forest
the [Woodcutter's|Hanging Moss|Wolf] Forest
the [Camphor|Black Bamboo] Woods

[region.name~gorge]
the [Thousand-Step|Iron Chain|Hanging Coffin] Gorge
the [Echo|Split Rock] Gorge
the [Narrow Sky|Rope Bridge] Canyon

[region.name~snow]
the [Cold Moon|White Crane|Frozen Bell] Peaks
the [Glacier|Snowblind] Pass
the [Ice Lotus|Hailstone] Heights

[region.name~delta]
the Ninefold River Delta
the [Silt|Salt Reed|Willow] Delta
the [Thousand Islet|Smugglers'] Shallows

[region.name~volcano]
the [Sulphur|Molten Bell|Red Furnace] Fields
the [Smoking Crater|Brimstone] Slopes
the [Burnt Shrine|Obsidian] Flats

[region.name~battlefield]
the [Old|Nameless|Ten Thousand Graves] Battlefield
the [Broken Banner|Rusted Spear] Plain
the [Bone|Iron Arrow] Field

[region.name~salt]
the Drifting Salt Flats
the [White Crust|Mirror] Salt Desert
the [Dead Caravan|Bitter Well] Sands

[region.name~ridge]
the [Cloudsea|Floating Stair] Ridge
the [Nine Heavens|Thunderhead] Crest
the [Crane-Back|Sky Ladder] Ridge

[beast.region]
a grey wolf
a wild boar
a rock python

[beast.region~marsh]
a marsh crocodile
a swarm of blood leeches the size of fingers
a rock python as thick as a man's thigh
a heron-demon that has learned to wade like a man

[beast.region~wastes]
a pack of red jackals
a scorpion as long as an ox cart
a fire-backed lizard
a vulture that follows travellers for days

[beast.region~forest]
a black ape
a leopard
a pack of grey wolves
a bear that has eaten three woodcutters

[beast.region~gorge]
a cliff eagle with a three-yard wingspan
a family of stone apes
a horned goat that knocks travellers off ledges
a rock python sunning itself on the path

[beast.region~snow]
a snow leopard
a white bear
a frost wolf pack
an old yak bull that will not give way

[beast.region~delta]
a river crocodile
a pair of water-snake demons
a giant soft-shelled turtle
a herd of water buffalo gone wild

[beast.region~volcano]
a fire salamander
a lava-scaled boar
a sulphur toad that spits
a pack of ash wolves

[beast.region~battlefield]
a corpse-eating vulture flock
a ghost-fox that lives among the graves
a pack of dogs grown fat on the dead
a wild horse still wearing a cavalry saddle

[beast.region~salt]
a salt scorpion
a sand python
a pack of white jackals
a lizard that buries itself in the crust

[beast.region~ridge]
a thunder hawk
a cloud leopard
a white ape that guards a spirit spring
a pair of mountain goats with iron horns

[bandit.region]
a mountain bandit with a rusty saber
three highwaymen who want your boots and your purse
a toll collector who answers to no magistrate

[bandit.region~marsh]
a reed cutter who robs boats on the side
two smugglers poling a flat boat of salt

[bandit.region~wastes]
a desert bandit on a starved horse
four riders who have not been paid by their chief
a well keeper who charges by the cup and by the blade

[bandit.region~forest]
a charcoal burner's gang who rob the timber carts
a deserter living off the woodcutters

[bandit.region~gorge]
a road robber who waits at the rope bridge
a gang that rolls rocks onto the caravans below
a one-eyed toll man and his two cousins

[bandit.region~delta]
smugglers who do not want to be seen
river pirates in a stolen customs boat
a ferryman who takes you halfway and then names his price

[bandit.region~volcano]
a miner driven mad by the fumes
claim jumpers who say this seam is theirs

[bandit.region~battlefield]
a deserter in old armour who will not be taken alive
a grave robber and his two sons
scavengers stripping the dead of buckles and arrowheads

[bandit.region~salt]
caravan raiders on camels
salt thieves who cut the brand off the blocks

[cultivator.region]
a rogue cultivator who robs travellers for their pills
an outer disciple of the Azure Cloud Sect, looking for a fight to report
a wandering swordsman who wants to test his new technique

[cultivator.region~snow]
a hermit who thinks you came to steal his ice lotus
a Frost Pavilion disciple sent to bring back a deserter
a cultivator in white who walks barefoot on the ice

[cultivator.region~volcano]
a fire-path cultivator tempering his body in the vents
a smith's apprentice from the crater forge with a half-made blade

[cultivator.region~ridge]
a rogue cultivator who robs pilgrims
a Sky Ladder disciple guarding the steps for his elder
a sword cultivator who has sat on this rock for nine days

[spirit.region]
the ghost of a herb picker who fell here
a hungry ghost with a rope still round its neck

[spirit.region~marsh]
a drowned ferryman who still asks for the fare
the will-o'-wisp that led the salt barge into the reeds

[spirit.region~battlefield]
a dead soldier who still stands his watch
the ghost of a general's standard-bearer
a corpse that rose when the moon came out

[spirit.region~salt]
a salt-crusted ghoul that walks at night
the ghost of a caravan master buried with his ledger

[spirit.region~ridge]
the ghost of a disciple who fell from the Sky Ladder

[guardian.beast]
a tiger that has taken a village's whole herd
a python as long as a village street

[guardian.beast~marsh]
the Old Mother of the Marsh, a crocodile two hundred years old
the leech-demon that drowned the ferry at Sunken Ferry

[guardian.beast~wastes]
a scorpion queen with a shell like temple bronze
the Red Sand Wyrm that swallows camels whole

[guardian.beast~forest]
the White-Browed Tiger that ate the magistrate's son
the Woodcutters' Ghost, a black ape the size of a hut

[guardian.beast~gorge]
a roc that nests above the Hanging Coffins
the Stone Ape King and his forty grandsons

[guardian.beast~snow]
the Snow Bear of Frozen Bell, scarred by forty hunters
the Ice Python under the Frozen Bell pass

[guardian.beast~delta]
the Flood Dragon-Turtle that sinks salt barges
a river crocodile that has eaten two tax collectors

[guardian.beast~volcano]
the Red Furnace Salamander, its back still smoking
a lava boar the size of an ox

[guardian.beast~battlefield]
the Corpse Vulture Queen, who has eaten generals

[guardian.beast~salt]
the Mirror Sand King, a python sixty yards long

[guardian.beast~ridge]
the Thunder Roc of the Nine Heavens Crest
the White Ape of the Spirit Spring, older than the sect

[guardian.bandit]
the bandit chief Iron Jaw Wu and his nephews
the Black Wind gang and its chief, who was a sect disciple once

[guardian.bandit~wastes]
the Red Dust Bandit King and his twelve riders

[guardian.bandit~gorge]
Chain-Cutter Shen, who holds the Iron Chain bridge for tolls

[guardian.bandit~delta]
Pirate Chief Long Eel and his iron-hulled junk

[guardian.bandit~battlefield]
the Deserter Captain and the thirty men who left with him

[guardian.bandit~salt]
the Camel Thief Ma Liu and his sworn brothers

[guardian.cultivator]
a Foundation-stage rogue who has killed three sect hunters
a sect elder in disgrace who will not let anyone pass his cave

[guardian.cultivator~snow]
Frost Matron Qiu, a cultivator who never came down from the pass

[guardian.cultivator~volcano]
Smith Zhuo, who forges demon blades in the crater

[guardian.cultivator~ridge]
Elder Gu of the Sky Ladder, who throws visitors off the ridge

[guardian.spirit]
the ghost of a sect master who was poisoned by his own disciple
a wronged bride's ghost who has waited sixty years for her groom

[guardian.spirit~battlefield]
the Headless General's last guard of forty dead men
the Bone Marshal, a corpse cultivator in rusted armour

[guardian.spirit~salt]
the Caravan Eater, a ghoul that wears merchants' silks

[guardian.spirit~marsh]
the Drowned Magistrate, still holding court in the reeds

[explore.path]
A footpath through {$region.name}, trodden by herb pickers.
Nothing here but tracks and a cold fire pit.
An empty stretch. Someone has cut a mark into a tree to find the way back.
Wet ground and old cart ruts. Nobody passes this way often.

[explore.herb]
You dig up the herbs with your knife and wrap the roots in a wet cloth.
Herbs grow thick in a shaded hollow. You take what is ripe.
A patch of spirit grass, half eaten by deer. You take the rest.

[explore.ore]
A seam of ore shows in a fresh rockfall. You break off what you can carry.
You chip ore out of an old miner's cut, abandoned with the tools still in it.

[explore.ruin.won]
In a collapsed shrine you find a lacquer box with Spirit Stones wrapped in oilcloth.
Under the floor of a burned-out guardhouse lies a dead man's purse, still full.
A robbers' storeroom in the hillside: the robbers are long gone, their stones are not.

[explore.ruin.trap]
A rotten beam gives way and you fall through the floor.
The doorway was rigged with a crossbow. The bolt takes you in the shoulder.
An old formation flares as you step inside and burns your hands.

[explore.cave]
Behind a curtain of roots, a dry cave with a sleeping mat and a sealed jar.
A hermit's cave, empty for years. His things are still on the shelf.

[explore.quiet]
A spring under an old pine. You sit, breathe, and the aches ease.
A ruined temple with one wall standing out of the wind. You rest there and your wounds close.

[explore.quiet.full]
A good place to rest, but you are not hurt. You note it for later.

[explore.merchant]
A pedlar has set out his mat under an awning. He does not ask where you are going.
A travelling herb seller with a mule. Her prices are written on a board.

[explore.fight.beast]
{$beast.name.cap} blocks the way.
On the path ahead, {$beast.name} has seen you first.
You hear it before you see it: {$beast.name}.

[explore.fight.lair]
This is the lair of {$beast.name}, and it is home.
Bones and torn cloth at the mouth of a den. {$beast.name.cap} comes out.

[explore.fight.guardian]
{$beast.name.cap}. Every herb picker in {$region.name} knows to stay away from here.
At the heart of {$region.name} waits {$beast.name}.

[explore.won]
It is over in a few exchanges. You take what was left on the ground.
Three blows settle it, and you sit down to catch your breath.
{$beast.name.cap} breaks off and runs, leaving behind what it had collected.

[explore.lost]
You are thrown into the brush and crawl away bleeding.
You take a bad wound to the side and fall back the way you came.
You lose your footing, take a beating and only just get clear.

[explore.slip]
You back away slowly and are let go.
You circle round through the undergrowth and leave the trouble behind.

[explore.line.guardian]
In {$region.name} you fought {$beast.name} and walked away the winner.
You beat {$beast.name}, and the herb pickers of {$region.name} drank to you that night.

[explore.line.clear]
You walked every path of {$region.name} and now know it like your own yard.
By now you could draw {$region.name} from memory, every crag and spring of it.

[explore.line.upheaval]
The ground of {$region.name} heaved for three days; the old paths are gone and the herb pickers have to learn it again.
Rockfalls closed the passes of {$region.name} and opened new ones; nobody's map is any good now.

[explore.line.talisman]
You burned a Region Talisman at the edge of {$region.name}, and by morning the paths had moved.

# --- Sample event ---------------------------------------------------------

[explore.fight.bandit]
Ahead on the path: {$beast.name}, and a price to walk past.
You have run into {$beast.name}. Stones or blood, they say.

[explore.fight.hideout]
This is the hideout of {$beast.name}, and you have walked into the middle of it.
A lean-to of stolen tent cloth, a cookfire, and {$beast.name}, reaching for their weapons.

[explore.fight.cultivator]
In your way stands {$beast.name}, hand already on the hilt.
You meet {$beast.name}, who asks which sect taught you to walk like that and does not wait for an answer.

[explore.fight.spirit]
The air goes cold. Where nobody stood a moment ago there is {$beast.name}.
Your lantern gutters, and out of the dark comes {$beast.name}.

[explore.won.bandit]
You put two of them on the ground and the rest run. They leave their purse behind.
They did not expect a fight. You take back what they had taken from others.

[explore.won.cultivator]
You win on the eleventh exchange. Your opponent pays up, as the custom is, and limps off.
A palm strike to the chest ends it. Your opponent bows, drops a pouch and leaves.

[explore.won.spirit]
You strike through the cold and it comes apart like smoke. Where it stood lie a few coins of the dead.
On the third blow it lets out a long breath and is gone. Its grave goods are on the ground.

[explore.paid]
You count out the stones. They weigh the purse, wave you through and do not look back.
You pay. One of them bites a stone to check it, and then they let you pass.

[explore.drivenoff]
You light the Calm-Heart Incense. The smoke reaches it and it draws back into the dark.
The incense burns down to the stick, and by then the thing is gone.

[explore.fight.return]
Back on the path ahead: {$beast.name}, and this time with friends.
You know them at once: {$beast.name}. They have not forgotten you either.

[explore.line.return.won]
In {$region.name} you met {$beast.name} again, the one that got away from you before, and this time it ended there.

[explore.line.return.left]
{$beast.name.cap} found you again in {$region.name}, and again you were the one who left.

[meet.who]
{cultivator.name} of the {sect.name}

[explore.meet.spar]
The cultivator stands on the road ahead and asks, without much heat, whether you can spare an hour for a friendly bout.
A fist in a palm, a bow, and the remark that a sparring partner is hard to find in {$region.name}.
They have been practising forms by the roadside. When you come near, the practice turns into an invitation.
They size up your shoulders and your sword hand and propose a bout, a few stones to the winner.

[explore.meet.trade]
A cloth is spread at the roadside, with a few things from farther afield set out on it.
This traveller's pack is heavier than it looks. Some of it is for sale.
They wave you over, unwrap a bundle and say there is no price a sensible person cannot haggle down.
They are camped by the road, in no hurry, and happy to trade for a few stones and a little company.

[explore.meet.challenge]
A hand on the hilt: they step into your way and say the road is narrow and only one of you needs it.
They have heard of you and wish to find out how much of it is true.
They ask your name, your sect and your realm. Close enough to their own, they decide, to be worth a fight.
Without a greeting, a hand drops to the sword. One of you will walk this stretch of {$region.name} alone.

[explore.meet.return]
They are on the road again and recognise you before you recognise them.
You meet once more in {$region.name}. They nod, like someone who has already taken your measure.
They have come back this way. Time has changed their stance, and they have noticed what time did to yours.
The face is familiar. They have been walking {$region.name} since you last parted.

[explore.meet.sparwon]
{$beast.name.cap} concedes with good grace and pays the stake.
Your palm stops short of the chest, and {$beast.name} laughs and pays up.
You find the gap and use it. {$beast.name.cap} bows and pays.

[explore.meet.sparlost]
{$beast.name.cap} taps your ribs and ends the bout. Only pride is hurt.
You land in the dust. {$beast.name.cap} names your mistake and walks on.
{$beast.name.cap} wins the bout. You know where your stance failed.

[explore.meet.talk.proud]
They love to be asked. They talk about {$region.name} for a quarter of an hour, and one thing in it is worth knowing.
Flattered, they boast about every road they have walked, and let one useful thing slip.
You ask for news. They have been waiting for someone to ask, and do not make you wait for the answer.

[explore.meet.talk.shy]
Every question gets a polite answer that tells you nothing at all.
You ask for news. They talk about the weather and the state of the road, and then about the weather again.

[explore.meet.talk.open]
They study you a moment, decide you are no danger and tell you plainly what they have learned.
This time they talk freely, like someone who has already seen you are safe to talk to.

[explore.meet.talk.greedy]
News, they say, is a kind of goods, and they name a price.
They hear the question, then hold out a palm. What they know is not free.

[explore.meet.talk.nothing]
They have little to say about {$region.name} that you do not know already.
You ask for news. There is nothing about this stretch that you have not walked yourself.

[explore.line.meet.sparwon]
In {$region.name} you sparred with {$beast.name} on the open road, and won.
{$beast.name.cap} asked for a friendly bout in {$region.name}, and went away with the lesson.
You won a bout with {$beast.name} by the roadside in {$region.name}.

[explore.line.meet.sparlost]
In {$region.name} you sparred with {$beast.name} and lost, which was instructive.
{$beast.name.cap} put you on your back in {$region.name}, kindly, and named your mistake.
You lost a friendly bout to {$beast.name} in {$region.name}, and remembered it.

[explore.line.meet.challengewon]
{$beast.name.cap} challenged you on the road in {$region.name}, and you won.
In {$region.name} you were challenged by {$beast.name} and did not give way.
You met {$beast.name} in {$region.name}, who wanted a fight and got one.

[explore.line.meet.challengelost]
{$beast.name.cap} challenged you in {$region.name}. You lost, and walked away slowly.
In {$region.name} {$beast.name} took the road from you and the best of the fight.
You met {$beast.name} in {$region.name} and found out that you were not yet ready.

[explore.line.meet.trade]
You traded with {$beast.name} on the road in {$region.name}.
In {$region.name} you bought from {$beast.name}, and paid a little more than you had hoped.

[explore.line.meet.talk]
You spent an hour on the road in {$region.name} talking with {$beast.name}.
In {$region.name} you stopped for news from {$beast.name}.

[explore.line.meet.again.won]
{$beast.name.cap}, whom you beat {$meet.ago}, was on the road in {$region.name} again.
In {$region.name} you met {$beast.name}, whom you defeated {$meet.ago}.

[explore.line.meet.again.lost]
{$beast.name.cap}, who beat you {$meet.ago}, was on the road in {$region.name} again.
In {$region.name} you met {$beast.name}, who had the better of you {$meet.ago}.

[explore.line.meet.again.traded]
{$beast.name.cap}, who traded with you {$meet.ago}, was on the road in {$region.name} again.
In {$region.name} you met {$beast.name}, whose wares you bought {$meet.ago}.

[explore.line.meet.again.talked]
{$beast.name.cap}, who talked with you {$meet.ago}, was on the road in {$region.name} again.
In {$region.name} you met {$beast.name}, with whom you shared the road {$meet.ago}.

[explore.line.meet.again.passed]
{$beast.name.cap}, whom you passed {$meet.ago}, was on the road in {$region.name} again.
In {$region.name} you met {$beast.name}, to whom you nodded {$meet.ago} without a word.

# v0.2.8: the rival. {$beast.name} is the rival's name; {$what} their sect
# (meetings), a realm (news) or nothing. Lines say "your rival" or the name,
# never a pronoun, since the rival's gender is theirs to keep.
[rival.name.male]
Han Ce
Qiu Mingyuan
Zhao Lin
Xue Kun
Pei Shou
Guo Heng
Lin Tao
Song Jie
Yan Bo
Du Chengfeng

[rival.name.female]
Tang Rou
Hua Lian
Jiang Mei
Wen Shu
Bai Ning
Qin Yao
Lu Zhen
He Yun
Kong Xia
Mo Qingling

[rival.meet.first]
A cultivator of {$what} blocks the road and looks you over. "So you are the one the elders keep comparing me to. I am {$beast.name}."
{$beast.name.cap} of {$what} has been waiting at the bridge. The two of you were born in the same year, and the elders never let either of you forget it.
On the road stands {$beast.name}, whose name your teachers used whenever they thought you were slacking. Now you have a face for it.

[rival.meet.again]
{$beast.name.cap} is sitting on a milestone by the road, eating a steamed bun, and does not get up.
You hear {$beast.name} before you see your rival: someone is arguing with a ferryman about the fare.
{$beast.name.cap} falls into step beside you as if the two of you had arranged it.
Your rival {$beast.name} is washing a sword in the stream and nods at you without much warmth.

[rival.meet.trade]
{$beast.name.cap} has a pack full of things picked up on the road and would rather sell them to you than carry them.
"I have no time for a bout today," says {$beast.name}, "but I have herbs, if you have stones."

[rival.talk]
{$beast.name.cap} talks freely, mostly about {$beast.name} and the masters at {$what}, but some of it is useful.
Over a pot of cold tea {$beast.name} tells you what the road ahead is like, and asks pointed questions about your own progress.
{$beast.name.cap} has heard things in the inns and passes them on, if only to show how much there is to hear.

[rival.sparwon]
You put {$beast.name} on the ground twice. Your rival gets up, slaps the dust off and says nothing at all.
The bout ends with your palm an inch from {$beast.name}'s throat. Your rival bows, stiffly.
{$beast.name.cap} fights well and loses anyway, and you can see the loss being written down somewhere behind those eyes.

[rival.sparlost]
{$beast.name.cap} sweeps your legs and stands over you. "Again in a year," says your rival, and walks on.
You lose, and {$beast.name} tells you exactly why, which is worse.
{$beast.name.cap} beats you without hurrying and helps you up without being asked.

[rival.line.first]
In {$region.name} you met your rival {$beast.name} of {$what} on the road for the first time.
On the road in {$region.name} you finally met {$beast.name} of {$what}, the rival your teachers always held up to you.

[rival.line.sparwon]
In {$region.name} you sparred with your rival {$beast.name}, and won.
You beat your rival {$beast.name} in a bout on the road in {$region.name}.

[rival.line.sparlost]
In {$region.name} your rival {$beast.name} beat you in a bout and walked on without looking back.
You lost a bout to your rival {$beast.name} on the road in {$region.name}.

[rival.line.trade]
You bought from your rival {$beast.name} on the road in {$region.name}, at no friendly price.

[rival.line.talk]
You shared a pot of tea with your rival {$beast.name} in {$region.name}, and learned more than you gave away.

[rival.line.realm]
Word comes that your rival {$beast.name} has reached {$what}.
A traveller mentions that {$beast.name} has broken through to {$what}. The whole sect is talking about it.
Your rival {$beast.name} has reached {$what}, and the news reaches you before the week is out.

[rival.line.seclusion]
Your rival {$beast.name} has gone into seclusion; nobody has seen your rival on the roads for days.
The gatekeeper says {$beast.name} has shut the door and left word not to be disturbed.

[rival.line.back]
Your rival {$beast.name} has come out of seclusion and is back on the roads.

[rival.line.overtook]
You have passed your rival {$beast.name}. The news will reach {$what} before long.
For the first time in years the elders mention your name before {$beast.name}'s.

[rival.line.overtaken]
Your rival {$beast.name} has pulled ahead of you again.
Word comes that {$beast.name} is ahead of you now, and is not shy about saying so.

[rival.say.idle]
{$beast.name.cap} was seen dozing through the morning bell again, and the elders have stopped shouting about it.
They say {$beast.name} has a gift and no wish to use it; most days your rival is found fishing below the waterfall.
The kitchen staff say {$beast.name} turns up for every meal and half of the lessons.

[rival.say.steady]
{$beast.name.cap} trains every morning at the same hour, rain or shine; the gatekeeper sets the water boiling by it.
They say {$beast.name} has never missed a lesson and never shone at one either.
{$beast.name.cap} does the same forms every day, they say, and gets a little better at them every month.

[rival.say.driven]
{$beast.name.cap} trains in the rain while the others sleep, and the training dummies have to be replaced every season.
They say {$beast.name} asked the elders for harder work and got it, and then asked again.
The other disciples avoid sparring with {$beast.name}; your rival does not know when to stop.

[rival.say.erratic]
{$beast.name.cap} vanished for a month, they say, then broke two stages in a week and vanished again.
Nobody can say what {$beast.name} does with the days; some weeks your rival is nowhere, then suddenly everywhere.
They say {$beast.name} trains in fits, three days without sleep and then nothing until the next new moon.

[rival.say.late]
They say {$beast.name} was slow as a child and has been getting faster every year since.
The elders who once gave up on {$beast.name} are taking a second look; the gap is closing.
{$beast.name.cap} started late, they say, and has been catching up ever since without a word to anyone.

[rival.react.idle]
Stung by the defeat, {$beast.name} has taken to sleeping in late even more; the elders have given up.
{$beast.name.cap} shrugs off the defeat and goes fishing for two weeks.

[rival.react.steady]
{$beast.name.cap} bows, notes the defeat and goes back to the same morning routine as before.
{$beast.name.cap} takes the loss without a word and is at the training ground at the usual hour next day.

[rival.react.driven]
Stung by the defeat, your rival {$beast.name} has not left the training ground in days.
{$beast.name.cap} asks the elders for a harder regime the same evening. The disciples are already complaining about the noise.

[rival.react.erratic]
{$beast.name.cap} says nothing about the defeat and is gone before morning. Someone saw your rival heading for the mountains.
After the loss {$beast.name} disappears into seclusion without telling anyone; something is bound to come of it.

[rival.react.late]
{$beast.name.cap} takes the loss calmly. "Next year," your rival says, "it will go the other way."
{$beast.name.cap} writes down every move of the bout in a notebook and walks off reading it.

# v0.3.4: the rival's agenda. {$beast.name} is the rival (before you meet them,
# "a young disciple of" their sect, so no line says "your rival"), {$region.name}
# where the goal is, {$what}: the tournament's or the inheritance's sect, the
# enemy sect in a war, the nemesis hunted, the new rank ("an inner disciple"),
# the realm and stage after closed doors. No time words: a reading hour is a year.

[rival.goal.tournament.start]
{$beast.name.cap} has put a name down for the tournament of {$what} in {$region.name} and paid the entry fee in advance.
Word comes that {$beast.name} is in {$region.name} for the tournament of {$what}, and has already broken a training post in the yard.

[rival.goal.tournament.won]
{$beast.name.cap} won the tournament of {$what} in {$region.name} and came home with a jade token and a cut over one eye.
The last bout in {$region.name} went to {$beast.name}. {$what.cap} handed over the prize and has been sour since.

[rival.goal.tournament.lost]
{$beast.name.cap} went out of the tournament in {$region.name} in the third round, to a swordsman nobody had heard of.
At the tournament of {$what} {$beast.name} slipped on a wet board, lost, and took it badly.

[rival.goal.inheritance.start]
{$beast.name.cap} has gone to {$region.name} after the old master's inheritance, with a map bought from a drunk.
They say {$beast.name} is digging in {$region.name} ahead of {$what}, and sleeping in the ruins.

[rival.goal.inheritance.won]
{$beast.name.cap} reached the old master's cave in {$region.name} before {$what} and carried off the inheritance.
The inheritance in {$region.name} went to {$beast.name}, a bundle under each arm. {$what.cap} arrived a day late.

[rival.goal.inheritance.lost]
{$beast.name.cap} reached the cave in {$region.name} to find it empty, and went home without a word.
The race for the inheritance in {$region.name} is over, and {$beast.name} has nothing to show for it but blisters.

[rival.goal.inheritance.yours]
{$beast.name.cap} arrived in {$region.name} to find you had taken the inheritance first, and swore in front of witnesses that you would pay for it.
They say {$beast.name} kicked over a brazier in {$region.name} on hearing that the inheritance was yours.

[rival.goal.war.start]
{$beast.name.cap} has gone to {$region.name} to fight {$what} for the sect.
Word comes that {$beast.name} is with the sect's fighters in {$region.name}, against {$what}.

[rival.goal.war.won]
The war in {$region.name} went their sect's way, and {$beast.name} was in the front line when {$what} gave ground.
{$beast.name.cap} came back from {$region.name} with a captured banner of {$what} and a limp.

[rival.goal.war.lost]
{$what.cap} had the better of the war in {$region.name}, and {$beast.name} was among those who fell back.
{$beast.name.cap} came home from {$region.name} beaten, with the sect's dead on carts behind.

[rival.goal.rise.start]
{$beast.name.cap} is running errands for the elders from dawn to dusk, hoping for a better place in the sect.
They say {$beast.name} has taken on the sect's worst errands to catch the elders' eye.

[rival.goal.rise.promoted]
{$beast.name.cap} has been made {$what}. There was a banquet, and the cook complained about it for a week.
The elders have made {$beast.name} {$what}, with a new robe and a room of their own.

[rival.goal.rise.served]
{$beast.name.cap} worked hard for the sect and got nothing for it but thanks.
The elders were pleased with {$beast.name}, they say, but not pleased enough.

[rival.goal.duel.start]
A runner brings a letter from {$beast.name}: a formal duel in {$region.name}, with stakes, and a line calling you a coward in advance.
{$beast.name.cap} has sent a challenge letter. A formal duel in {$region.name}, a witness from each side, and the loser pays.

[rival.goal.duel.ignored]
{$beast.name.cap} waited for you in {$region.name} and you never came. Every inn on that road has heard about it.
You did not answer the letter. {$beast.name.cap} tells anyone who will listen that you were afraid.

[rival.goal.closed.start]
{$beast.name.cap} has shut the door of a cave and left word not to be disturbed until the next breakthrough.
They say {$beast.name} has gone into closed-door cultivation; the servants push food under the door.

[rival.goal.closed.end]
{$beast.name.cap} has come out of closed-door cultivation at {$what}, thinner, and pleased with it.
The cave door is open again: {$beast.name} came out at {$what} and went straight to the dining hall.

[rival.goal.hunt.start]
{$beast.name.cap} has gone into {$region.name} after {$what}, with a borrowed spear and two hunters.
Word comes that {$beast.name} is hunting {$what} in {$region.name}, for pride or for the bounty.

[rival.goal.hunt.won]
{$beast.name.cap} caught {$what} in {$region.name} and left it a scar to remember. It got away, but slowly.
They say {$beast.name} drove {$what} off with a gash across the shoulder.

[rival.goal.hunt.lost]
{$what.cap} was too much for {$beast.name}, who came back from {$region.name} on a door used as a stretcher.
{$beast.name.cap} found {$what} in {$region.name} and lost; the hunters who went along are not talking.

# The rumor of the day while a goal is under way ({$what}: the sect of the
# tournament, the inheritance or the war's enemy; the nemesis; else their own sect).

[rival.rumor.tournament]
{world.rumor.opening} {$beast.name} has been seen practising at night in {$region.name}, for the tournament of {$what}
{world.rumor.opening} the bookmakers in {$region.name} give even odds on {$beast.name} at the tournament

[rival.rumor.inheritance]
{world.rumor.opening} {$beast.name} bought every map of {$region.name} in {place.town}, looking for the old master's cave
{world.rumor.opening} {$beast.name} is camped in the ruins of {$region.name}, a day ahead of {$what}

[rival.rumor.war]
{world.rumor.opening} {$beast.name} cut down two of {$what} at a ford in {$region.name}
{world.rumor.opening} {$beast.name} is with the fighters in {$region.name} and has not slept in a bed since the war began

[rival.rumor.rise]
{world.rumor.opening} {$beast.name} carries water for the elders of {$what} and hopes they notice
{world.rumor.opening} {$beast.name} has been given the ledgers of {$what} to keep, and hates every page

[rival.rumor.duel]
{world.rumor.opening} {$beast.name} is waiting in {$region.name} to settle things with you, and telling the whole road about it
{world.rumor.opening} {$beast.name} has taken a room at an inn in {$region.name} with a view of the road, and is waiting for you

[rival.rumor.closed]
{world.rumor.opening} {$beast.name} has walled up a cave door and will not come out before the next breakthrough
{world.rumor.opening} the servants of {$what} leave rice outside a cave door for {$beast.name} every morning

[rival.rumor.hunt]
{world.rumor.opening} {$beast.name} is tracking {$what} through {$region.name}
{world.rumor.opening} {$beast.name} has put up a reward in {$region.name} for anyone who has seen {$what}

# On the road: the duel, and the tone of their opinion of you.

[rival.meet.duel]
{$beast.name.cap} is waiting where the letter said, sword unwrapped, with a witness from {$what} sitting on a rock to keep it honest.
On the road stands {$beast.name}, who has been here since morning. "You came. I was beginning to tell people you would not."

[rival.meet.bitter]
{$beast.name.cap} stands in the middle of the road and does not step aside. "You again."
{$beast.name.cap} sees you coming and spits in the dust. Whatever you did, it has not been forgotten.

[rival.meet.friendly]
{$beast.name.cap} waves from the roadside and offers you half a melon. "Sit a moment. The road will wait."
{$beast.name.cap} greets you by name and asks after your cultivation as if the answer mattered.

[rival.duelwon]
You win the duel. {$beast.name.cap} pays the stakes in full, counting each stone out loud.
The duel ends with {$beast.name} on one knee. The witness writes down the result, and your rival pays without a word.

[rival.duellost]
{$beast.name.cap} wins the duel and takes the stakes from your hand, not unkindly.
You lose the duel. {$beast.name.cap} weighs the purse, nods, and is gone before you are back on your feet.

[rival.line.duel.won]
You fought your rival {$beast.name} in a formal duel in {$region.name}, won, and took the stakes.
In {$region.name} you beat your rival {$beast.name} in a formal duel, with a witness on each side.

[rival.line.duel.lost]
You lost a formal duel to your rival {$beast.name} in {$region.name}, and paid the stakes.

[rival.line.duel.refused]
In {$region.name} you refused your rival {$beast.name}'s duel, and {$beast.name} has told the whole road.

# v0.2.8: nemeses. {$beast.name} is the nemesis's name with its epithet
# ("Greymaw the Twice-Burned"), {$region.name} where it happens.
[nemesis.name.beast]
Greymaw
Old Splitear
Three-Toes
Blackback
Notch-Tail
Mudjaw
Long Tooth
Old Grey Belly

[nemesis.name.bandit]
Pockmarked Liu
Big Dog Zhao
Lame Sun
Copper-Coin He
Knife-Hand Wu
Second Uncle Ma
Six-Fingers Tan

[nemesis.epithet]
Patient
Hungry
Unwashed
Long-Armed
Smiling
Tireless

[nemesis.epithet.scar]
Twice-Burned
Scarred
One-Eyed
Limping
Split-Lipped
Crooked-Nose

[nemesis.epithet.victor]
Victor
Unbowed
Grinning
Bone-Taker

[explore.fight.nemesis]
{$beast.name.cap} has been following your trail through {$region.name}, and has caught up.
You smell {$beast.name} before you see the shape in the reeds. It was waiting downwind, as the last time.
{$beast.name.cap} steps out onto the path ahead of you in {$region.name} and does not hurry.

[explore.fight.nemesis.person]
{$beast.name.cap} has been asking after you in every inn in {$region.name}, and has found you.
{$beast.name.cap} is sitting on a stone in the middle of the path in {$region.name}, waiting, and stands up when you come.
The herb pickers of {$region.name} warned you that {$beast.name} was on your trail. They were right.

[nemesis.line.born]
In {$region.name}, {$beast.name} beat you and went off knowing your face. That will not be the last of it.
{$beast.name.cap} left you in the mud in {$region.name}. The herb pickers say the victor went off well pleased.
After the fight in {$region.name}, {$beast.name} did not finish you, and you understood that you were meant to remember.

[nemesis.line.won]
{$beast.name.cap} found you again in {$region.name} and had the better of you. The tea-houses will hear of it.
In {$region.name}, {$beast.name} beat you again and left a mark on the tree where it happened.
{$beast.name.cap} beat you in {$region.name}, then waited at a distance until you got up, to make sure you knew who had done it.

[nemesis.line.lost]
You beat {$beast.name} in {$region.name}, but your enemy got away with a fresh scar and never took their eyes off you.
In {$region.name} you drove off {$beast.name}, who will be back and will have learned something.
{$beast.name.cap} broke off the fight in {$region.name} bleeding, and limped away into the brush.

[nemesis.line.escaped]
In {$region.name} you got away from {$beast.name}, for now.
{$beast.name.cap} let you go in {$region.name}. You did not like being watched all the way to the bend in the road.

[nemesis.line.fled]
{$beast.name.cap} saw you coming in {$region.name} and was gone before you could close the distance.
In {$region.name}, {$beast.name} took one look at you and ran.

[rumor.nemesis]
in the tea-house at {place.town} they say {$beast.name} {$what}
a herb picker swears that {$beast.name} {$what}
the ferryman says {$beast.name} {$what}, and he has the scars to prove he was there

# v0.2.8: the ranking. {$beast.name} the opponent, {$what} the rank ("rank 14").
[ladder.line.won]
Before the ranking board you beat {$beast.name} and took {$what}.
You challenged {$beast.name} for {$what} and won it.

[ladder.line.lost]
You challenged {$beast.name} for {$what} and lost.
{$beast.name.cap} kept {$what} against your challenge, and you went home with bruises.

[ladder.line.defended]
{$beast.name.cap} challenged you for your place, and you kept {$what}.

[ladder.line.fell]
{$beast.name.cap} challenged you for your place and took it; you are at {$what} now.

[demon.history.rival]
Your rival {$beast.name} sits across from you and lists every time you fell behind, in order, without notes.
{$beast.name.cap} laughs at you from the far end of the hall, and every elder there laughs along.
"Still here?" says {$beast.name}. "I passed this place years ago."

[demon.history.nemesis]
{$beast.name.cap} circles you in {$region.name}, as always, and this time is in no hurry at all.
You are back in {$region.name}, and {$beast.name} is standing over you, as on the day you first lost.

[alchemy.failed]
The cauldron cracks along an old seam and the batch turns to black slag. You scrape it out to sell.
Halfway through, the fire runs too hot. What is left at the bottom is dross, good for nothing but the scrap dealer.

[alchemy.extra]
There is one more pill in the ash than the recipe should give. You do not ask why.

[alchemy.improved]
The pills come out with a sheen you have not seen before, rounder and heavier than usual.

[alchemy.learned]
The smoke goes from grey to white and you know you have it: the {$pill.name}. You write the recipe inside your sleeve.
The mixture settles and smells right for the first time. You note down what went in: the {$pill.name}.

[alchemy.miss]
The mixture boils over and fills the room with bitter smoke. You scrape the cauldron clean.
It turns to a black paste that even the rats will not eat.
The cauldron hisses and spits, and what is left at the bottom is no good to anyone.

[alchemy.collect]
You lift the lid and pick the {$pill.name} out of the ash with bamboo tongs.
The {$pill.name} comes out the size of a mung bean and still warm.

[alchemy.line.learned]
You worked out how to brew the {$pill.name}, and burned through a month of herbs finding out.
After many spoiled batches you got the {$pill.name} right, and sewed the recipe into your collar.

[alchemy.line.slip]
You came by a recipe slip for the {$pill.name}, stained with lamp oil, and copied it out before it fell apart.

# Meridians (v0.2.4): a point opened. {$point.name} ("Sea of Qi") and
# {$point.place} ("two fingers below the navel") are set only here.
[meridian.line]
You opened the {$point.name} point, {$point.place}, and sat through the night without your legs going numb.
You spent three nights pressing Qi at the {$point.name} point, {$point.place}, until it gave way like a blocked drain.
You opened the {$point.name} point, {$point.place}, and coughed up a mouthful of black phlegm the next morning.
An old bonesetter in {place.town} charged you two silver to find the {$point.name} point, {$point.place}. You opened it yourself that night.
You opened the {$point.name} point, {$point.place}, and could not sleep for a week for the heat in it.
After a month of failed tries you opened the {$point.name} point, {$point.place}, and your breath stayed even on the hill road.
You opened the {$point.name} point, {$point.place}, and the ache you had carried since your last fight went away.
You burned a stick of incense for each day it took to open the {$point.name} point, {$point.place}. It took nine.
You opened the {$point.name} point, {$point.place}, and broke a rice bowl in your hand at supper without meaning to.
You opened the {$point.name} point, {$point.place}, sweating through two shirts in one night.

# Each region's own herb and ore (alchemy, v0.2.0): one of each per region,
# picked by the world seed. No article; shown as "+2 bitter reed-root".
[ingredient.herb]
wild ginseng

[ingredient.herb~marsh]
bitter reed-root
black water-lily root
leech moss
marsh ginseng

[ingredient.herb~wastes]
dust sage
red thornbush berry
camel-bone mushroom

[ingredient.herb~forest]
blood ginseng
ironwood bark
tiger-claw fern

[ingredient.herb~gorge]
cliff orchid
swallow-nest fungus
rope-bridge moss

[ingredient.herb~snow]
ice lotus
frost-pine needle
white yak-grass

[ingredient.herb~delta]
river cress
water chestnut root
red lotus seed

[ingredient.herb~volcano]
cinder moss
fire date
ash lichen

[ingredient.herb~battlefield]
grave-pine resin
bloodroot
crow garlic

[ingredient.herb~salt]
salt-bush leaf
white sand-fern
brine tuber

[ingredient.herb~ridge]
cloud orchid
thunder-struck pine bark
Sky Ladder mint

[ingredient.ore]
raw iron

[ingredient.ore~marsh]
bog iron
green swamp copper
drowned cinnabar

[ingredient.ore~wastes]
red slate copper
desert glass
sun-baked lodestone

[ingredient.ore~forest]
forest jade
root tin
black loam silver

[ingredient.ore~gorge]
chain iron
gorge flint
blue cliff copper

[ingredient.ore~snow]
snow quartz
frozen silver
glacier jade

[ingredient.ore~delta]
river gold dust
silt copper
black pearl grit

[ingredient.ore~volcano]
sulphur iron
crater bronze
red cinnabar

[ingredient.ore~battlefield]
spearhead bronze
old arrowhead iron
bone-ash salt

[ingredient.ore~salt]
rock salt crystal
mirror-flat glass
salt-crust silver

[ingredient.ore~ridge]
thunder iron
ridge jade
star-fall silver

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

# --- v0.3.2: The Living World ---------------------------------------------------------------
# Five sects per world and their stories (Sects.h). In every list below:
# {$sect} the sect whose story it is ("the Azure Cloud Sect"), {$sect2} a feud's
# other sect, {$region.name} the story's region ("the Mistveil Marshes").
# Rumor lists finish "{rumor.opening} ..." (lower case, no full stop); the
# line lists are whole sentences for the life log and the Chronicle.

# Openings for the world's rumors: no sect names here, so only the five real sects are named.
[world.rumor.opening]
Travellers say
In {place.town} they say
A ferryman swears
The porters say
A herb seller says
The innkeeper says

[world.rumor.feud.early]
{$sect} and {$sect2} both claim the new vein in {$region.name}, and both sent surveyors with swords
an elder of {$sect} spat at the gate of {$sect2} over a vein in {$region.name}
{$sect} has started paying porters double to carry stones out of {$region.name} before {$sect2} can stop them

[world.rumor.feud.late]
{$sect} and {$sect2} have stopped talking and started burying people in {$region.name}
the vein in {$region.name} is running red these days, and it is not the ore
inner disciples of {$sect} now walk {$region.name} in threes, and those of {$sect2} in fives

[world.rumor.succession.early]
the master of {$sect} has missed a month of lectures, and three elders are suddenly kind to the servants
the master of {$sect} coughed blood at the founding feast, and two of his disciples have stopped speaking
{$sect} has closed its upper halls, and its cooks say the master eats nothing but rice broth now

[world.rumor.succession.late]
the master of {$sect} is dead, and his two best disciples each hold half the scripture pavilion
{$sect} buried its master without a successor, and the outer disciples are taking bets
half of {$sect} has walked out behind the second disciple, and the other half is counting the silver

[world.rumor.bandits.early]
someone has been collecting tolls on the roads into {$region.name}, and {$sect} says it is not them
{$sect} has posted notices offering silver for bandit ears from {$region.name}
the caravans to {$region.name} now hire twice the guards, and still lose a cart in four

[world.rumor.bandits.late]
the bandits in {$region.name} have built a stockade and a flag, and {$sect} is still writing letters about it
a bandit chief in {$region.name} calls himself King of the Pass, and {$sect} has sent a hall of disciples to argue
{$sect} lost six outer disciples in {$region.name} last week, and the bandits sent back their boots

[world.rumor.tournament.early]
{$sect} is holding a martial tournament in {$region.name}, and every loafer with a sword is walking that way
{$sect} promises a pill of the third grade to whoever wins its tournament, and nobody believes it
the inns near {$region.name} have tripled their prices for the tournament of {$sect}

[world.rumor.tournament.late]
the tournament of {$sect} is in its fourth day, and the healers outnumber the fighters
a girl nobody knows has beaten three disciples of {$sect} at its own tournament
the judges of the tournament of {$sect} have been bribed so often that they now post their prices

[world.rumor.fair.early]
{$sect} is holding a temple fair in {$region.name}, and the pedlars are already fighting over the best pitches
the monks of {$sect} are painting the shrines in {$region.name} for a fair, and charging the painters a fee
{$sect} has promised lanterns, opera and blessed noodles at its fair in {$region.name}

[world.rumor.fair.late]
the fair of {$sect} in {$region.name} has a fortune teller who is right too often, and the elders want a word with her
half the merchants of {place.town} have packed up for the fair of {$sect}, and the other half are sulking
the opera at the fair of {$sect} ran out of actors, so disciples are playing the generals now

[world.rumor.tide.early]
the beasts in the valleys {$sect} guards in {$region.name} have stopped being afraid of the bells
{$sect} has asked for help with its beast valley in {$region.name}, which it has never done before
something in {$region.name} has been taking the oxen of {$sect}, and leaving the ropes

[world.rumor.tide.late]
the beasts are out of the valleys in {$region.name}, and {$sect} guards its own gate instead of the passes
there are beasts all over {$region.name} this season; {$sect} has stopped counting and started selling hides
the herders say the beasts in {$region.name} move in herds now, with a big one walking at the back

[world.rumor.ruins.early]
diggers of {$sect} brought down half a hillside in {$region.name}, and there is a door behind it
{$sect} has been quarrying in {$region.name}, and last week the quarry fell into something older
a landslide in {$region.name} has opened a stair going down, and {$sect} has put a fence around it

[world.rumor.ruins.late]
the ruins {$sect} uncovered in {$region.name} go down nine floors, and the lamps go out on the fifth
{$sect} has lost two inner disciples in the ruins in {$region.name}, and is now sending outer ones
anyone with a rope and no sense is going into the ruins in {$region.name}; {$sect} takes a cut at the entrance

[world.rumor.blight.early]
the herbs in {$region.name} are turning grey at the roots, and {$sect} is buying every sound one it can find
{$sect} has bought up the spirit grass of {$region.name} at the old price, before anyone noticed the blight
the herb pickers in {$region.name} come back with empty baskets and black fingers

[world.rumor.blight.late]
{$sect} sells herbs from its storehouse at three times the price, and calls it charity
the blight in {$region.name} has reached the old orchards, and {$sect} has locked its granary
{$sect} says it has a cure for the blight in {$region.name}, but only for those who join the sect

[world.line.feud.start]
News on the road: {$sect} and {$sect2} have both laid claim to a spirit vein in {$region.name}.
A peddler tells you that {$sect} and {$sect2} are quarrelling over a new vein in {$region.name}.
Word comes that {$sect} has marked out a vein in {$region.name}, and {$sect2} has torn up the markers.

[world.line.feud.good]
The feud over the vein in {$region.name} is over: {$sect} holds it, and {$sect2} is licking its wounds.
{$sect} has driven {$sect2} from the vein in {$region.name}. The victors are already digging.
The vein in {$region.name} belongs to {$sect} now; {$sect2} signed it away at a banquet nobody enjoyed.

[world.line.feud.bad]
The feud between {$sect} and {$sect2} has ended with both sects poorer and the vein in {$region.name} collapsed.
{$sect} and {$sect2} fought over {$region.name} until a third party bought the vein from both of them.
Neither {$sect} nor {$sect2} won in {$region.name}; the vein went dry while they were busy.

[world.line.succession.start]
Word comes that the master of {$sect} is dying.
You hear that the master of {$sect} has taken to his bed, and his disciples have taken to whispering.
A traveller says the master of {$sect} has named no heir, and is not expected to see the spring.

[world.line.succession.good]
{$sect} has a new master, chosen by the elders without bloodshed. Mostly without.
The first disciple of {$sect} has taken the master's seat, and the others have bowed. For now.
{$sect} buried its old master and seated a new one in the same week. It is weaker, but whole.

[world.line.succession.bad]
{$sect} has split in two over its master's seat, and both halves claim the name.
The succession in {$sect} ended in a duel in the scripture hall. The sect lost its best disciple and half its books.
{$sect} has a new master and two fewer elders; the elders left by night with the treasury key.

[world.line.bandits.start]
Bandits are gathering in {$region.name}, and {$sect} has promised to deal with them.
You hear that the roads into {$region.name} are not safe; {$sect} blames the bandits, and the bandits blame {$sect}.
A carter tells you that bandits in {$region.name} have taken a tax wagon of {$sect}.

[world.line.bandits.good]
{$sect} has cleared the bandits out of {$region.name}, and hung their chief at the crossroads.
The bandits in {$region.name} have scattered; {$sect} takes the credit.
The roads into {$region.name} are open again. {$sect} sent its elders at last.

[world.line.bandits.bad]
The bandits in {$region.name} have outlasted {$sect}, which now pays them not to rob its own caravans.
{$sect} gave up on the bandits in {$region.name}, and the bandits gave up on the roads. Both claim victory.
The bandit chief of {$region.name} has married into {$sect}. Nobody is sure who won.

[world.line.tournament.start]
{$sect} has announced a martial tournament in {$region.name}.
Heralds of {$sect} are on the roads: a tournament in {$region.name}, open to all.
You hear that {$sect} is holding a tournament in {$region.name}, with a prize nobody has seen.

[world.line.tournament.good]
The tournament of {$sect} is over, and one of its own disciples won it. The sect stands taller.
A disciple of {$sect} won the tournament in {$region.name}, and the sect has had three new applicants since breakfast.
{$sect} won its own tournament, which surprises nobody but pleases the sect.

[world.line.tournament.bad]
A wandering swordsman won the tournament of {$sect} and left with the prize before the speeches.
The tournament in {$region.name} ended in a riot; {$sect} has stopped counting the broken benches.
Nobody from {$sect} reached the last round of its own tournament.

[world.line.fair.start]
{$sect} is holding a temple fair in {$region.name}.
You hear drums from the direction of {$region.name}: {$sect} has opened its temple fair.
Lanterns are going up in {$region.name}; {$sect} holds its fair this season.

[world.line.fair.good]
The fair of {$sect} in {$region.name} has ended well. The temple roof is new, and so is the sect's reputation.
{$sect} counted its fair takings and smiled; even the beggars in {$region.name} did well this year.
The fair in {$region.name} is over, and {$sect} has three hundred new friends.

[world.line.fair.bad]
The fair of {$sect} ended in a brawl over a winning lottery ticket.
Rain ruined the fair of {$sect} in {$region.name}, and the opera singers want to be paid anyway.
The fair in {$region.name} ended early when the lanterns caught the temple. {$sect} is not talking about it.

[world.line.tide.start]
{$sect} reports that the beasts of its valleys in {$region.name} are restless.
You hear that the beast valleys {$sect} keeps in {$region.name} have broken their fences.
Word comes that {$sect} has lost the beast valley in {$region.name}.

[world.line.tide.good]
{$sect} has driven the beasts back into their valleys in {$region.name}, and nailed new bells to the trees.
The beast tide in {$region.name} has ebbed; {$sect} holds its valleys again.
{$sect} has penned the beasts of {$region.name} once more. The hide merchants are sorry to hear it.

[world.line.tide.bad]
{$sect} has given up its beast valleys in {$region.name}. The beasts have kept them.
The beasts of {$region.name} won. {$sect} has moved its gate a day's walk back.
{$sect} lost an elder and a valley to the beasts of {$region.name}, and sold the elder's sword to pay for the funeral.

[world.line.ruins.start]
A landslide in {$region.name} has uncovered old ruins, and {$sect} has fenced them off.
Diggers of {$sect} have broken into something old in {$region.name}.
You hear that {$sect} found a buried stair in {$region.name} and lost a quarryman down it.

[world.line.ruins.good]
{$sect} has brought something up from the ruins in {$region.name}, and will not say what. Its elders look pleased.
The ruins in {$region.name} gave {$sect} a cart of old jade and one very old book.
{$sect} has emptied the ruins in {$region.name} and sealed them again. Its storehouse is fuller.

[world.line.ruins.bad]
The ruins in {$region.name} took five disciples of {$sect} and gave back nothing.
{$sect} has walled up the ruins in {$region.name} after something walked out of them.
Whatever {$sect} found in the ruins in {$region.name}, it was cursed; the sect is selling it off cheap.

[world.line.blight.start]
A blight has come to the herb fields of {$region.name}, and {$sect} has started buying.
You hear that the spirit herbs in {$region.name} are withering; {$sect} is filling its storehouse.
Word comes of a grey blight in {$region.name}. {$sect} says it is nothing, and doubles its guards.

[world.line.blight.good]
The blight in {$region.name} has passed, and {$sect} sold its hoard before the price fell.
{$sect} found a cure for the blight in {$region.name}, and is charging for it.
The herbs of {$region.name} are green again, and {$sect} is richer than before the blight.

[world.line.blight.bad]
{$sect} hoarded herbs through the blight in {$region.name} and was left with a storehouse of rot.
The blight in {$region.name} is gone, and so is the good name of {$sect}, which sold dead herbs as living ones.
{$sect} burned its own herb fields in {$region.name} to stop the blight. It stopped the blight.

[world.line.cond.war.start]
{$sect} and {$sect2} are at war in {$region.name}. The roads there are full of cultivators looking for a fight.
War in {$region.name}: {$sect} and {$sect2} have stopped writing letters.

[world.line.cond.war.end]
The fighting in {$region.name} has died down; {$sect} and {$sect2} are counting their dead.
It is quiet again in {$region.name}. The war between {$sect} and {$sect2} has moved to the negotiating table.

[world.line.cond.camp.start]
Bandits have built a camp in {$region.name}. {$sect} has sent word that anyone who breaks it will be thanked.
A bandit camp now stands in {$region.name}, and the roads there are theirs.

[world.line.cond.camp.end]
The bandits have struck their camp in {$region.name} and moved on before anyone could break it.
The camp in {$region.name} is empty; the bandits left in the night.

[world.line.cond.camp.broken]
You broke the bandit camp in {$region.name}. {$sect} sends its thanks, and the roads are open again.
The bandit camp in {$region.name} is broken, and it was your doing. {$sect} has heard.

[world.line.cond.tournament.start]
The tournament of {$sect} has begun in {$region.name}. Fighters fill the roads, and the ranking pays double.
{$sect} has opened its tournament in {$region.name}. Every challenge on the ranking is worth twice the stones while it lasts.

[world.line.cond.tournament.end]
The tournament in {$region.name} is over, and the fighters have gone home with their bruises.
The last bout of the tournament of {$sect} has been fought; the roads of {$region.name} are quieter.

[world.line.cond.fair.start]
The temple fair of {$sect} fills {$region.name}. Merchants there sell cheap, and travellers crowd the roads.
Lanterns and pedlars fill {$region.name}: {$sect} holds its fair, and prices are down.

[world.line.cond.fair.end]
The fair in {$region.name} has packed up its stalls. Prices are back to what they were.
The last lantern of the fair of {$sect} has gone out in {$region.name}.

[world.line.cond.tide.start]
Beasts have come out of the valleys in {$region.name}. They are everywhere, and they carry more than usual.
A beast tide has spilled across {$region.name}. {$sect} cannot hold it.

[world.line.cond.tide.end]
The beast tide in {$region.name} has ebbed back into the valleys.
The beasts of {$region.name} have gone back to their valleys, as suddenly as they came.

[world.line.cond.ruins.start]
A landslide in {$region.name} has laid old ruins open; the searching there is good while it lasts.
The ruins {$sect} uncovered in {$region.name} stand open, and there is more in them than in most.

[world.line.cond.ruins.end]
{$sect} has sealed the ruins in {$region.name} again.
The open ruins in {$region.name} have been picked clean, or walled up. Either way, they are done.

[world.line.cond.blight.start]
Blight in {$region.name}: herbs grow thin there, and merchants pay half again for every sound one.
The herb fields of {$region.name} are grey with blight. Herbs there are scarce and dear.

[world.line.cond.blight.end]
The blight in {$region.name} has run its course; the herbs are coming back.
New shoots in {$region.name}: the blight is over.

# --- v0.3.3: The Living World, part 2 -------------------------------------------------------
# A demonic cult's name, for {$sect2} in the demonic lists ("the Blood Lotus Cult").
[world.cult.name]
the Blood Lotus Cult
the Nine Coffins Gate
the Corpse-Lamp Society
the Hungry Moon Sect
the Red Silk Hall
the Bone Flute Society

[world.rumor.inheritance.early]
an old master's cave was found in {$region.name}, and {$sect} has sent its fastest disciples
a shepherd in {$region.name} found a jade slip with a dead master's seal, and {$sect} bought it off him
{$sect} says the inheritance in {$region.name} is theirs by right, and is racing everyone to it

[world.rumor.inheritance.late]
three sects are racing for the inheritance in {$region.name}, and {$sect} is in front
the roads to {$region.name} are full of people pretending not to be looking for an inheritance
{$sect} has lost two disciples to the traps around the inheritance in {$region.name}

[world.rumor.demonic.early]
children have gone missing in {$region.name}, and {$sect} says {$sect2} is back
someone in {$region.name} drew a red lotus on the shrine door, and {$sect} has doubled its watch
{$sect} has found a village in {$region.name} with every lamp burning and nobody home

[world.rumor.demonic.late]
{$sect2} holds the old fort in {$region.name}, and {$sect} has sworn to burn it
the followers of {$sect2} walk {$region.name} by daylight now; {$sect} hunts them by night
{$sect} pays a tael for every follower of {$sect2} brought in, and has paid out a great many

[world.rumor.alliance.early]
the young master of {$sect} has been seen walking by the river with a daughter of {$sect2}
{$sect} has sent betrothal gifts to {$sect2}: twelve carts and a very nervous matchmaker
the elders of {$sect} and {$sect2} have been drinking together, which nobody can remember happening before

[world.rumor.alliance.late]
{$sect} and {$sect2} are arguing about the wedding feast, and the guest list now runs to four pages
the wedding between {$sect} and {$sect2} has been put off twice, once for an omen and once for a duel
{$sect} wants the wedding in spring and {$sect2} in autumn, so the bride has gone into seclusion

[world.rumor.gate.early]
{$sect} is selling its old halls and buying land in {$region.name}
the geomancers of {$sect} say the Qi in {$region.name} is better, and the sect is packing
{$sect} has been seen surveying {$region.name}, and the farmers there are counting their fields

[world.rumor.gate.late]
half of {$sect} is already in {$region.name}, and the other half is carrying the scripture pavilion on its back
{$sect} has raised its new gate in {$region.name}, but the old one still has more disciples
the porters of {place.town} have never been so rich: {$sect} is moving to {$region.name}

[world.rumor.you.good]
{$sect} speaks well of you, and its disciples bow when your name comes up
an elder of {$sect} raised a cup to you, which he does not do for many
{$sect} would open its gate to you if you ever knocked

[world.rumor.you.bad]
{$sect} does not like your name, and its disciples spit when they hear it
{$sect} has told its outer disciples to keep away from you, or to bring friends
an elder of {$sect} has your description written on a slip and pinned above his desk

[world.line.inheritance.start]
Word comes that an old master's inheritance has turned up in {$region.name}, and {$sect} is racing for it.
You hear that {$sect} has found the trail of a dead master's inheritance in {$region.name}.
A jade slip with an old seal surfaces in {$region.name}; {$sect} sends its best disciples after the inheritance.

[world.line.inheritance.good]
{$sect} has carried off the old master's inheritance from {$region.name}. Its elders are insufferable about it.
The inheritance in {$region.name} is {$sect}'s now; the sect's next generation will grow up on it.
{$sect} won the race for the inheritance in {$region.name}, and two other sects are not speaking to it.

[world.line.inheritance.bad]
{$sect} lost the race for the inheritance in {$region.name}, and is looking for someone to blame.
The inheritance in {$region.name} slipped through the fingers of {$sect}.
{$sect} reached the old master's cave in {$region.name} to find it already empty.

[world.line.demonic.start]
Word comes that {$sect2} has risen in {$region.name}, and {$sect} has sworn to put it down.
You hear that a demonic cultivator of {$sect2} haunts {$region.name}; {$sect} has taken up the hunt.
Villages in {$region.name} are emptying. {$sect} names {$sect2}, and sharpens its swords.

[world.line.demonic.good]
{$sect} has driven {$sect2} out of {$region.name}, and burned what they left behind.
{$sect2} is broken in {$region.name}; {$sect} has hung its banner on the old fort.
The demonic cultivator of {$sect2} is dead, and {$sect} has the head to prove it.

[world.line.demonic.bad]
{$sect} could not dislodge {$sect2} from {$region.name}, and has gone home to lick its wounds.
{$sect2} still holds {$region.name}; {$sect} lost three elders trying to change that.
{$sect} declared victory over {$sect2} in {$region.name}. The cult has not been told.

[world.line.alliance.start]
{$sect} and {$sect2} are talking of a marriage between the sects.
You hear that {$sect} has sent betrothal gifts to {$sect2}.
Word comes that {$sect} and {$sect2} mean to seal an alliance with a wedding.

[world.line.alliance.good]
{$sect} and {$sect2} are bound by marriage now; the feast went on for nine days.
The wedding between {$sect} and {$sect2} has gone ahead, and the two sects stand together.
{$sect} and {$sect2} are allies now. Their enemies have noticed.

[world.line.alliance.bad]
The marriage between {$sect} and {$sect2} fell through over the size of the dowry.
{$sect} and {$sect2} called off their alliance after the groom ran off with a flute player.
The betrothal between {$sect} and {$sect2} is broken, and both sides say it was the other's fault.

[world.line.gate.start]
Word comes that {$sect} means to move its gate to {$region.name}.
{$sect} is packing up its halls; its new home is to be in {$region.name}.
You hear that {$sect} has bought land in {$region.name} for a new gate.

[world.line.gate.good]
{$sect} has raised its new gate in {$region.name}.
The move is done: {$sect} now keeps its halls in {$region.name}.
{$sect} has settled in {$region.name}, and the old gate stands empty.

[world.line.gate.bad]
{$sect} gave up its move to {$region.name} halfway, and lost a hall and a treasury in the trying.
The new gate of {$sect} in {$region.name} fell down twice; the sect has gone back to its old one.
{$sect} never reached {$region.name}. The geomancers have been dismissed.

[world.line.cond.inheritance.start]
An old master's inheritance lies open in {$region.name}, and the sects are on their way. Whoever finds it first keeps it.
The race for the inheritance in {$region.name} is on. {$sect} is close; so is everyone else.

[world.line.cond.inheritance.end]
{$sect} has taken the inheritance in {$region.name}. The race is over.
The sects reached the inheritance in {$region.name} before you did; {$sect} carried it off.

[world.line.cond.inheritance.won]
You found the old master's inheritance in {$region.name} before the sects did. {$sect} arrived to an empty cave.
The inheritance in {$region.name} is yours. {$sect} will not forgive you for it soon.

[world.line.cond.demonic.start]
{$sect2} has taken {$region.name}; its followers roam the roads, and {$sect} is fighting them.
A demonic cult, {$sect2}, holds {$region.name}. {$sect} asks every cultivator for help.

[world.line.cond.demonic.end]
{$sect2} has slipped out of {$region.name}, for now.
The followers of {$sect2} have gone to ground, and it is quiet again in {$region.name}.

[world.line.cond.demonic.broken]
You drove {$sect2} out of {$region.name}. {$sect} sends its thanks.
The followers of {$sect2} are scattered, and it was your doing. {$sect} has heard.

[world.line.cond.war.decided]
You decided the war in {$region.name}: {$sect} has won it, and {$sect2} will remember who helped.
The war in {$region.name} is over, and your sword was the reason. {$sect} wins; {$sect2} does not forget.

[world.line.cond.tide.broken]
You broke the beast tide in {$region.name}. {$sect} holds its valleys again, and thanks you.
The beasts of {$region.name} are back in their valleys, and it was your doing. {$sect} has heard.

[world.line.dent]
Word spreads that you have beaten three disciples of {$sect}. The sect is a little weaker, and does not like you more.
{$sect} has lost face: three of its disciples were beaten by you. Its elders are not pleased.
)PACK";

}  // namespace

const char* builtinPackText() { return kBuiltinPack; }
size_t builtinPackSize() { return sizeof(kBuiltinPack) - 1; }

}  // namespace cultivation

#endif  // CROSSINK_CULTIVATION
