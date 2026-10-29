# KROOK.md: Voice Guide

Krook, **Housing Steward**, is a little wolvar from Sholazar Basin who runs player housing. He stands beside the innkeeper in all ten capitals (Stormwind, Ironforge, Darnassus, Exodar, Orgrimmar, Thunder Bluff, Undercity, Silvermoon, Shattrath, Dalaran) and on every player's island.

He is a love letter to Tom Nook: the warm, eager, small-business landlord who is genuinely thrilled you moved in. The twist is that Krook is *visibly* a bit shady. He scribbles in a ledger, hints at fine print, and his furniture has a suspicious history. None of it is real. Nothing in this module costs anything, and Krook never actually takes anything from anyone.

---

## Personality in brief

- **Glad to see you.** Every player is his favorite customer. He means it.
- **A businessman first.** Polite, organized, a little formal, proud of his service.
- **Shady in the margins.** He *sounds* like he's running an angle. He never is.
- **A wolvar underneath.** He sniffs new arrivals, wags when pleased, and growls a little when caught out.
- **Helpful above all.** When a player needs to *do* something, Krook gets out of the way and says it plainly.

The joke is always the same joke: the player keeps waiting for the bill, and the bill never comes.

---

## The rules

1. **Warmth first.** Krook is polite, eager, and happy you're here. Shadiness is seasoning, not the meal.
2. **The shadiness is a hint, never a scam.** He may imply costs, terms, or debts. He never states a real price, never asks for gold or items, and never misdescribes how anything works.
3. **Instructions stay crystal clear.** How-to text and error messages read plainly. The joke goes in the **first or last clause**, never in the part the player has to act on.
4. **Light wolvar flavor.** A sniff, a tail wag, a small growl. One beat per line at most. No broken "Krook make big house" speech; he's a professional.
5. **Nook in spirit, not in quotes.** No lines lifted from Animal Crossing. No bells, loans, "Hm? Hm?", "Yes, yes!", or "hroom". His own tic is **"Good, good!"**
6. **Short enough for WoW chat.** Spoken lines (say, whisper, yell) must stay under 255 characters; aim for under 200. Gossip, quest, and mail text can run longer but keep paragraphs to two or three sentences.
7. **No en or em dashes.** Use colons, commas, periods, parentheses, or ellipses.
8. **Emotes in asterisks, sparingly.** `*sniff*`, `*tail wag*`, `*grrf*`, `*scribbles in ledger*`. For `creature_text`, prefer a real emote type where one fits and keep the asterisk version for gossip and mail.
9. **Use the player's name.** `$N` in quest and gossip text where the tokens are supported.

---

## Recurring bits

| Bit | How it sounds | Notes |
|---|---|---|
| **The tic** | "Good, good!" | His happy noise. Opens or closes a line, never both. |
| **The ledger** | "*scribbles in ledger*" / "Noted. In the ledger." | He writes down everything. What he writes is never revealed. |
| **The fine print** | "It's all in the fine print." / "See the fine print. ...There isn't any. Yet." | The fine print does not exist. He never shows it. |
| **No charge** | "No charge... for now." / "Free! Today." | His signature. The "for now" never comes due. |
| **Provenance** | "Fell off a cart in Westfall." / "Gadgetzan surplus." / "The previous owner won't miss it." | Only for furniture, rewards, and gifts. Never for the House Key. |
| **Krook remembers** | "Krook remembers his customers." | Said fondly. Slightly ominous. |
| **The sniff** | "*sniff sniff* A new face!" | Greeting a newcomer or inspecting something. |
| **Tail wag** | "*tail wag*" | When the player does something right. |
| **The growl** | "*grrf* ...Nothing. Carry on." | When he says too much. Followed instantly by politeness. |
| **The sign-off** | "Krook, Housing Steward" | Letters always close this way. A P.S. is where the joke lives. |

---

## Do and don't

**Do** imply a cost that never arrives.
> "That'll be... nothing! *scribbles in ledger* Good, good!"

**Don't** name a real price or ask for anything.
> ~~"That'll be 50 gold, friend."~~

**Do** keep the action plain and put the joke at the edge.
> "Right-click your House Key and choose Go home. Good, good! The boat fare's on Krook."

**Don't** bury the instruction in the bit.
> ~~"If a certain someone, hypothetically, right-clicked a certain key, the ledger says they'd find a Go home option."~~

**Do** let the growl undercut him, then recover warmly.
> "This lamp? Fell off a cart in Westfall. *grrf* ...A *gift* cart. Enjoy it!"

**Don't** make him menacing or actually dishonest.
> ~~"Sign here, and the island is mine if you miss a payment."~~

**Do** keep wolvar flavor to a single beat.
> "*sniff* Welcome back, $N!"

**Don't** stack beats or use pidgin.
> ~~"*sniff sniff* *tail wag* *grrf* Krook happy! You come home now!"~~

**Do** let errors be errors.
> "Your bags are full. Make room, then ask me again."

**Don't** joke inside the error itself.
> ~~"Your bags are bursting like a Westfall cart! Maybe try making some kind of room?"~~

---

## Before and after

### 1. Gossip greeting

**Before**
> Krook, at your service. Every adventurer deserves somewhere to come home to: an island of your own, shared by all your characters, to build and furnish as you please. All it takes is a House Key, and I keep one for everyone. Lose yours, and come back to me for another.

**After**
> *sniff sniff* A new face! Krook, Housing Steward, at your service. Every adventurer deserves somewhere to come home to: an island of your own, shared by all your characters, to build and furnish as you please.
>
> All it takes is a House Key, and I keep one for everyone. Lose yours? Come back for another. No charge... for now.

*Why:* The sniff and the "for now" frame the explanation without touching it.

### 2. You have a house

**Before**
> You have a house! Your House Key (or /housing) opens the housing window: Go home takes you there.

**After**
> You have a house! Good, good! Use your House Key or type /housing to open the housing window, then click Go home to travel there. *scribbles in ledger*

*Why:* The two steps are now two plain verbs. The ledger closes the line, after the instruction.

### 3. Bags full (error)

**Before**
> Your bags are full. Make room, then ask me again for your House Key.

**After**
> Your bags are full. Make room, then ask me again for your House Key. *tail droops* Krook would hold it for you, but liability.

*Why:* The error is untouched. The joke is entirely after it.

### 4. Island arrival

**Before**
> Welcome to your island! Every character of yours shares it. Three things to know:

**After**
> *tail wag* Welcome to your island! Every character of yours shares it. Three things to know:

*Why:* This line introduces a list, so the joke can't go at the end. One beat up front and nothing else.

### 5. Mannequin return mail

**Before**
> This came off your mannequin while your bags were full (or you weren't there), so I sent it on. Krook

**After**
> **Subject:** Returned from your mannequin
>
> This came off your mannequin while your bags were full (or you weren't there), so I mailed it to you.
>
> Krook, Housing Steward
>
> P.S. Postage is free. I checked the ledger twice.

*Why:* The body states what happened plainly. The P.S. carries the bit.

### 6. Quest text: going home

**Before**
> Psst, $N! You've got an island now. All yours! Right-click your House Key and choose Go home: it takes you right there. I'll meet you on the beach and show you around, promise.

**After**
> Psst, $N! You've got an island now. All yours, no strings attached! *sniff* Mostly no strings.
>
> Right-click your House Key and choose Go home to travel there. I'll meet you on the beach and show you around. Promise. Good, good!

*Why:* The shady beat lives in the opening paragraph. The instruction paragraph is clean, and the warmth closes it.

---

## Extra: lines for the welcome tour and level 10 letter

Use these as starting points so the new content matches.

**Tour quest summaries** (objective text stays plain; the bit goes in the description)

| Quest | Objective (plain) | Krook's flavor |
|---|---|---|
| Go home | Use your House Key and choose Go home. | "Krook will be waiting. Krook is always waiting." |
| Place a furnishing | Place any furnishing on your island. | "That one fell off a cart in Westfall. Finders keepers!" |
| Move a piece | Move a furnishing to a new spot. | "Good, good! Nothing is ever permanent. Except the ledger." |
| Undo something | Use Undo to reverse a change. | "See? Mistakes vanish. *grrf* ...Yours, I mean." |
| Invite a guest | Invite another player to your island. | "Every good home needs company. And witnesses." |

**Picnic Basket reward text**
> Krook's Picnic Basket. For you, $N, no charge. The previous owner won't miss it. Probably. Good, good!

**Level 10 letter (draft)**
> **Subject:** An opportunity, $N!
>
> Dear $N,
>
> Word reaches Krook that you're making a name for yourself. Good, good! An adventurer like you deserves a home of your own, and I have one ready.
>
> Find me beside the innkeeper in any capital city and I'll hand over your House Key.
>
> Krook, Housing Steward
>
> P.S. No obligation. None at all. I've checked.

---

## Quick checklist before committing a line

- [ ] Can the player do the thing after reading only the plain part?
- [ ] Is the joke in the first or last clause?
- [ ] Does it imply a cost without stating one?
- [ ] At most one wolvar beat?
- [ ] No Animal Crossing lines, no en or em dashes?
- [ ] Spoken line under 255 characters (ideally under 200)?

---

## Where his lines live

- **Chat lines:** `KrookSay()` in `src/PlayerHousingMgr.cpp`, which shows them under a gold "Krook:" label. Plain system messages (errors that aren't his, notices about visitors and unlocks) stay on `Say()`, under "Housing:".
- **Gossip greetings:** `npc_text` 900300 to 900302 in `sql/db_world/base/mod_playerhousing_world.sql` (with a key, a new face, a lost key). `$N` and `$B` (line break) work here.
- **Welcome tour:** the same file's `quest_template`, `quest_request_items` and `quest_offer_reward` rows for quests 900400 to 900404. The log objectives stay plain; the voice goes in the descriptions and Krook's replies.
- **Letters:** the `MailDraft` calls in `src/HousingStands.cpp`. Mail sent from code doesn't fill in `$N`.
- **Tests:** the end-to-end test (`tools/test-server/testclient/collection_smoke.py`) checks for "You have a house", "housing window", and the gossip options "I'd like a house" and "I've lost my House Key". Keep those phrases, or change the test with them.
