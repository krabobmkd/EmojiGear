/*
 * emojibox.c  –  Emoji preset insertion (F-key) + emoji-table popup window.
 *
 * F-key layout (4 modifier banks × 10 keys = 40 entries):
 *   F1-F10              → row 0
 *   Shift+F1-F10        → row 1
 *   Ctrl+F1-F10         → row 2
 *   Ctrl+Shift+F1-F10   → row 3
 *
 * The popup window shows the current emoji set as an 11×5 grid:
 *   • row 0 / col 0 = corner (empty)
 *   • row 0 / cols 1-10 = column headers  "F1" … "F10"
 *   • col 0 / rows 1-4 = row headers      "-", "Shift", "Ctrl", "Sh+Ctrl"
 *   • remaining 40 cells = one emoji each
 * A chooser gadget in the top bar selects the active emoji set.
 */

#include <string.h>
#include <stdio.h>

#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <proto/utility.h>

#include <proto/layout.h>
#include <gadgets/layout.h>
#include <proto/button.h>
#include <gadgets/button.h>
#include <proto/chooser.h>
#include <gadgets/chooser.h>
#include <proto/window.h>
#include <classes/window.h>
#include <proto/label.h>
#include <images/label.h>
#include <proto/bevel.h>
#include <images/bevel.h>

#include <devices/inputevent.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <intuition/icclass.h>
#include <intuition/screens.h>

#include "compilers.h"
/* emojigear.h includes emojibox.h and provides the full App struct + app extern */
#include "emojigear.h"
#include "gadgetid.h"
#include "boopsimainwindow.h"

#include <gadgets/unitexteditor.h>
#include <proto/unitexteditor.h>

#include <gadgets/unibutton.h>
#include <proto/unibutton.h>

#include "bdbprintf.h"
#ifdef STATIC_UTF8RASTPORT
    #include "staticutf8rastport.h"
#else
    #include <libraries/utf8rastport.h>
    #include <proto/utf8rastport.h>
#endif

/* =========================================================================
 * Emoji set tables  (40 entries each, row-major: idx = row*10 + col)
 * =========================================================================
 */

/* Popular Emojis – matches popularEmojiTable used by F-key handler */
static const char *popularEmojiTable[40] = {
    /* row 0: F1-F10, no modifier */
    "\xF0\x9F\x98\x80", /* 😀 U+1F600 */
    "\xF0\x9F\x98\x8A", /* 😊 U+1F60A */
    "\xF0\x9F\x98\x8D", /* 😍 U+1F60D */
    "\xF0\x9F\xA4\x94", /* 🤔 U+1F914 */
    "\xF0\x9F\x98\x8E", /* 😎 U+1F60E */
    "\xF0\x9F\xA5\xB3", /* 🥳 U+1F973 */
    "\xF0\x9F\x98\x8B", /* 😋 U+1F60B */
    "\xF0\x9F\x98\x8F", /* 😏 U+1F60F */
    "\xF0\x9F\x98\x82", /* 😂 U+1F602 */
    "\xF0\x9F\x98\xAA", /* 😪 U+1F62A */
    /* row 1: Shift */
    "\xF0\x9F\xA4\xA9", /* 🤩 U+1F929 */
    "\xF0\x9F\x98\x85", /* 😅 U+1F605 */
    "\xE2\x9D\xA4",     /* ❤  U+2764  */
    "\xF0\x9F\x91\x8D", /* 👍 U+1F44D */
    "\xF0\x9F\x99\x8F", /* 🙏 U+1F64F */
    "\xF0\x9F\x91\x80", /* 👀 U+1F440 */
    "\xF0\x9F\x9A\x80", /* 🚀 U+1F680 */
    "\xF0\x9F\x8E\x89", /* 🎉 U+1F389 */
    "\xF0\x9F\x92\xAA", /* 💪 U+1F4AA */
    "\xF0\x9F\x8D\x95", /* 🍕 U+1F355 */
    /* row 2: Ctrl */
    "\xF0\x9F\xA5\x82", /* 🥂 U+1F942 */
    "\xF0\x9F\x8D\xBE", /* 🍾 U+1F37E */
    "\xF0\x9F\x92\xAA", /* 💪 U+1F4AA */
    "\xE2\x98\x95",     /* ☕ U+2615  */
    "\xF0\x9F\x93\xB1", /* 📱 U+1F4F1 */
    "\xF0\x9F\x92\xBB", /* 💻 U+1F4BB */
    "\xF0\x9F\x92\xBE", /* 💾  */
    "\xF0\x9F\xA4\x96", /* robot */
    "\xF0\x9F\xA6\x84", /* 🦄 U+1F984 */
    "\xF0\x9F\x8C\x88", /* 🌈 U+1F308 */
    "\xF0\x9F\x8C\xB8", /* 🌸 U+1F338 */
    /* row 3: Ctrl+Shift */
    "\xF0\x9F\x92\xA9", /* 💩 U+1F4A9 */
    "\xF0\x9F\x8E\xB8", /* 🎸 U+1F3B8 */
    "\xF0\x9F\x92\x8E", /* 💎 U+1F48E */
    "\xF0\x9F\x8C\x99", /* 🌙 U+1F319 */
    "\xE2\xAD\x90",     /* ⭐ U+2B50  */
    "\xF0\x9F\x8E\xB5", /* 🎵 U+1F3B5 */
    "\xF0\x9F\x8C\x8D", /* 🌍 U+1F30D */
    "\xF0\x9F\x95\x99", /* 🕙 U+1F559 */
    "\xF0\x9F\x92\xA1", /* 💡 U+1F4A1 */
};

/* Urban Emojis – transports, city buildings, workers */
static const char *urbanEmojiTable[40] = {
    /* row 0: vehicles */
    "\xF0\x9F\x9A\x97", /* 🚗 car        */
    "\xF0\x9F\x9A\x95", /* 🚕 taxi       */
    "\xF0\x9F\x9A\x8C", /* 🚌 bus        */
    "\xF0\x9F\x9A\x8E", /* 🚎 trolleybus */
    "\xF0\x9F\x8F\x8E", /* 🏎 race car   */
    "\xF0\x9F\x9A\x93", /* 🚓 police car */
    "\xF0\x9F\x9A\x91", /* 🚑 ambulance  */
    "\xF0\x9F\x9A\x92", /* 🚒 fire truck */
    "\xF0\x9F\x9A\x90", /* 🚐 minibus    */
    "\xF0\x9F\x9A\x9A", /* 🚚 truck      */
    /* row 1: other transport */
    "\xF0\x9F\x9A\x82", /* 🚂 locomotive  */
    "\xE2\x9C\x88",     /* ✈  airplane   */
    "\xF0\x9F\x9A\x81", /* 🚁 helicopter */
    "\xF0\x9F\x9B\xB8", /* 🛸 flying saucer */
    "\xF0\x9F\x9A\xB2", /* 🚲 bicycle    */
    "\xF0\x9F\x9B\xB4", /* 🛴 kick scooter */
    "\xF0\x9F\x9B\xB5", /* 🛵 motor scooter */
    "\xF0\x9F\x8F\x8D", /* 🏍 motorcycle */
    "\xF0\x9F\x9A\x80", /* 🚀 rocket     */
    "\xF0\x9F\x9B\xBB", /* 🛻 pickup truck */
    /* row 2: buildings */
    "\xF0\x9F\x8F\x99", /* 🏙 cityscape  */
    "\xF0\x9F\x8F\xA2", /* 🏢 office bldg */
    "\xF0\x9F\x8F\xAC", /* 🏬 dept store */
    "\xF0\x9F\x8F\xA6", /* 🏦 bank       */
    "\xF0\x9F\x8F\xA8", /* 🏨 hotel      */
    "\xF0\x9F\x8F\xA9", /* 🏩 love hotel */
    "\xF0\x9F\x8F\xAA", /* 🏪 convenience store */
    "\xF0\x9F\x8F\xAB", /* 🏫 school     */
    "\xF0\x9F\x8F\x9B", /* 🏛 classical building */
    "\xF0\x9F\x95\x8C", /* 🕌 mosque     */
    /* row 3: workers & tools */
    "\xF0\x9F\x91\xB7", /* 👷 construction worker */
    "\xF0\x9F\x91\xAE", /* 👮 police officer */
    "\xF0\x9F\x92\xBC", /* 💼 briefcase  */
    "\xF0\x9F\x94\xA7", /* 🔧 wrench     */
    "\xF0\x9F\x94\xA8", /* 🔨 hammer     */
    "\xF0\x9F\x8F\x97", /* 🏗 construction */
    "\xF0\x9F\x9A\xA7", /* 🚧 roadwork   */
    "\xF0\x9F\x97\xBA", /* 🗺 world map  */
    "\xF0\x9F\x93\xAB", /* 📫 mailbox    */
    "\xF0\x9F\x9A\x89", /* 🚉 train station */
};

/* Nature Emojis – flowers, plants, vegetables, landscapes */
static const char *natureEmojiTable[40] = {
    /* row 0: flowers & grass */
    "\xF0\x9F\x8C\xB8", /* 🌸 cherry blossom */
    "\xF0\x9F\x8C\xBA", /* 🌺 hibiscus   */
    "\xF0\x9F\x8C\xBB", /* 🌻 sunflower  */
    "\xF0\x9F\x8C\xB9", /* 🌹 rose       */
    "\xF0\x9F\x8C\xB7", /* 🌷 tulip      */
    "\xF0\x9F\x8C\xBC", /* 🌼 blossom    */
    "\xF0\x9F\x8C\xBF", /* 🌿 herb       */
    "\xF0\x9F\x8D\x80", /* 🍀 four-leaf clover */
    "\xF0\x9F\x8C\xB1", /* 🌱 seedling   */
    "\xF0\x9F\x8C\xBE", /* 🌾 sheaf of rice */
    /* row 1: trees & fungi */
    "\xF0\x9F\x8D\x84", /* 🍄 mushroom   */
    "\xF0\x9F\x8C\xB5", /* 🌵 cactus     */
    "\xF0\x9F\x8C\xB4", /* 🌴 palm tree  */
    "\xF0\x9F\x8C\xB2", /* 🌲 evergreen  */
    "\xF0\x9F\x8C\xB3", /* 🌳 deciduous  */
    "\xF0\x9F\x8E\x8B", /* 🎋 tanabata   */
    "\xF0\x9F\x8E\x8D", /* 🎍 pine deco  */
    "\xF0\x9F\x8D\x81", /* 🍁 maple leaf */
    "\xF0\x9F\x8D\x82", /* 🍂 fallen leaf */
    "\xF0\x9F\x8D\x83", /* 🍃 leaves     */
    /* row 2: vegetables */
    "\xF0\x9F\xA5\x95", /* 🥕 carrot     */
    "\xF0\x9F\xA5\xA6", /* 🥦 broccoli   */
    "\xF0\x9F\x8C\xBD", /* 🌽 corn       */
    "\xF0\x9F\x8D\x85", /* 🍅 tomato     */
    "\xF0\x9F\xA5\x91", /* 🥑 avocado    */
    "\xF0\x9F\x8D\x86", /* 🍆 eggplant   */
    "\xF0\x9F\xA5\x94", /* 🥔 potato     */
    "\xF0\x9F\xA7\x85", /* 🧅 onion      */
    "\xF0\x9F\xA7\x84", /* 🧄 garlic     */
    "\xF0\x9F\x8C\xB6", /* 🌶 pepper     */
    /* row 3: landscapes */
    "\xE2\x9B\xB0",     /* ⛰ mountain   */
    "\xF0\x9F\x8F\x94", /* 🏔 snow-capped mtn */
    "\xF0\x9F\x97\xBB", /* 🗻 mount fuji */
    "\xF0\x9F\x8C\x8B", /* 🌋 volcano    */
    "\xF0\x9F\x8F\x9D", /* 🏝 island     */
    "\xF0\x9F\x8F\x9C", /* 🏜 desert     */
    "\xF0\x9F\x8C\x8A", /* 🌊 wave       */
    "\xF0\x9F\x8C\x85", /* 🌅 sunrise    */
    "\xF0\x9F\x8C\x84", /* 🌄 sunrise over mtns */
    "\xF0\x9F\x8C\x8C", /* 🌌 milky way  */
};

/* Symbols – currencies, arrows, punctuation glyphs */
static const char *symbolsEmojiTable[40] = {
    /* row 0: currencies */
    "$",                 /* dollar       */
    "\xE2\x82\xAC",     /* € euro       */
    "\xC2\xA3",         /* £ pound      */
    "\xC2\xA5",         /* ¥ yen        */
    "\xE2\x82\xA3",     /* ₣ franc      */
    "\xE2\x82\xA4",     /* ₤ lira       */
    "\xE2\x82\xA6",     /* ₦ naira      */
    "\xE2\x82\xA9",     /* ₩ won        */
    "\xE2\x82\xAA",     /* ₪ shekel     */
    "\xE2\x82\xAB",     /* ₫ dong       */
    /* row 1: arrows */
    "\xE2\x86\x92",     /* → right      */
    "\xE2\x86\x90",     /* ← left       */
    "\xE2\x86\x91",     /* ↑ up         */
    "\xE2\x86\x93",     /* ↓ down       */
    "\xE2\x86\x94",     /* ↔ left-right */
    "\xE2\x86\x95",     /* ↕ up-down    */
    "\xE2\x87\x92",     /* ⇒ double right */
    "\xE2\x87\x90",     /* ⇐ double left  */
    "\xE2\x87\x91",     /* ⇑ double up    */
    "\xE2\x87\x93",     /* ⇓ double down  */
    /* row 2: marks & suits */
    "\xE2\x9C\x93",     /* ✓ check mark */
    "\xE2\x9C\x97",     /* ✗ cross      */
    "\xE2\x98\x85",     /* ★ black star */
    "\xE2\x98\x86",     /* ☆ white star */
    "\xE2\x99\xA5",     /* ♥ heart suit */
    "\xE2\x99\xA6",     /* ♦ diamond suit */
    "\xE2\x99\xA0",     /* ♠ spade suit */
    "\xE2\x99\xA3",     /* ♣ club suit  */
    "\xC2\xA9",         /* © copyright  */
    "\xC2\xAE",         /* ® registered */
    /* row 3: misc punctuation */
    "\xE2\x84\xA2",     /* ™ trademark  */
    "\xC2\xB0",         /* ° degree     */
    "\xC2\xA7",         /* § section    */
    "\xC2\xB6",         /* ¶ pilcrow    */
    "\xE2\x80\xA0",     /* † dagger     */
    "\xE2\x80\xA1",     /* ‡ double dagger */
    "\xE2\x80\xA2",     /* • bullet     */
    "\xE2\x80\xA6",     /* … ellipsis   */
    "\xE2\x88\x9E",     /* ∞ infinity   */
    "\xE2\x89\xA0",     /* ≠ not equal  */
};

/* Sports Emojis */
static const char *sportsEmojiTable[40] = {
    /* row 0: ball sports */
    "\xE2\x9A\xBD",     /* ⚽ soccer    */
    "\xF0\x9F\x8F\x80", /* 🏀 basketball */
    "\xF0\x9F\x8F\x88", /* 🏈 football  */
    "\xE2\x9A\xBE",     /* ⚾ baseball  */
    "\xF0\x9F\x8E\xBE", /* 🎾 tennis   */
    "\xF0\x9F\x8F\x90", /* 🏐 volleyball */
    "\xF0\x9F\x8F\x89", /* 🏉 rugby    */
    "\xF0\x9F\x8E\xB1", /* 🎱 8-ball   */
    "\xF0\x9F\x8F\x93", /* 🏓 ping pong */
    "\xF0\x9F\x8F\xB8", /* 🏸 badminton */
    /* row 1: combat & action */
    "\xF0\x9F\xA5\x8A", /* 🥊 boxing glove */
    "\xF0\x9F\xA5\x8B", /* 🥋 martial arts */
    "\xF0\x9F\xA4\xBC", /* 🤼 wrestling */
    "\xF0\x9F\xA4\xB8", /* 🤸 gymnastics */
    "\xF0\x9F\x8F\x8A", /* 🏊 swimming  */
    "\xF0\x9F\x9A\xB4", /* 🚴 cycling  */
    "\xF0\x9F\x8F\x83", /* 🏃 running  */
    "\xF0\x9F\xA7\x97", /* 🧗 climbing */
    "\xF0\x9F\xA4\xBA", /* 🤺 fencing  */
    "\xE2\x9B\xB7",     /* ⛷ skiing   */
    /* row 2: trophies & games */
    "\xF0\x9F\x8F\x86", /* 🏆 trophy   */
    "\xF0\x9F\xA5\x87", /* 🥇 gold medal */
    "\xF0\x9F\xA5\x88", /* 🥈 silver medal */
    "\xF0\x9F\xA5\x89", /* 🥉 bronze medal */
    "\xF0\x9F\x8F\x85", /* 🏅 sports medal */
    "\xF0\x9F\x8E\xAF", /* 🎯 bullseye */
    "\xF0\x9F\x8E\xB3", /* 🎳 bowling */
    "\xF0\x9F\x8F\x8B", /* 🏋 weightlifting */
    "\xF0\x9F\xA4\xBE", /* 🤾 handball */
    "\xF0\x9F\x8F\x84", /* 🏄 surfing  */
    /* row 3: winter & misc */
    "\xF0\x9F\xA7\x98", /* 🧘 yoga/meditation */
    "\xF0\x9F\x8F\x82", /* 🏂 snowboarding */
    "\xF0\x9F\xA4\xBF", /* 🤿 diving mask */
    "\xF0\x9F\x8E\xBF", /* 🎿 skis     */
    "\xE2\x9B\xB8",     /* ⛸ ice skate */
    "\xF0\x9F\x8F\x8C", /* 🏌 golf     */
    "\xF0\x9F\x8F\xB9", /* 🏹 bow & arrow */
    "\xF0\x9F\xA5\x85", /* 🥅 goal net */
    "\xF0\x9F\x8F\x91", /* 🏑 field hockey */
    "\xF0\x9F\x8F\x92", /* 🏒 ice hockey */
};

/* Katakana – first 40 of the 46 standard kana (a-i-u-e-o order) */
static const char *katakanaTable[40] = {
    /* row 0: a-ka row */
    "\xE3\x82\xA2", /* ア a  */
    "\xE3\x82\xA4", /* イ i  */
    "\xE3\x82\xA6", /* ウ u  */
    "\xE3\x82\xA8", /* エ e  */
    "\xE3\x82\xAA", /* オ o  */
    "\xE3\x82\xAB", /* カ ka */
    "\xE3\x82\xAD", /* キ ki */
    "\xE3\x82\xAF", /* ク ku */
    "\xE3\x82\xB1", /* ケ ke */
    "\xE3\x82\xB3", /* コ ko */
    /* row 1: sa-ta row */
    "\xE3\x82\xB5", /* サ sa */
    "\xE3\x82\xB7", /* シ si */
    "\xE3\x82\xB9", /* ス su */
    "\xE3\x82\xBB", /* セ se */
    "\xE3\x82\xBD", /* ソ so */
    "\xE3\x82\xBF", /* タ ta */
    "\xE3\x83\x81", /* チ ti */
    "\xE3\x83\x84", /* ツ tu */
    "\xE3\x83\x86", /* テ te */
    "\xE3\x83\x88", /* ト to */
    /* row 2: na-ha row */
    "\xE3\x83\x8A", /* ナ na */
    "\xE3\x83\x8B", /* ニ ni */
    "\xE3\x83\x8C", /* ヌ nu */
    "\xE3\x83\x8D", /* ネ ne */
    "\xE3\x83\x8E", /* ノ no */
    "\xE3\x83\x8F", /* ハ ha */
    "\xE3\x83\x92", /* ヒ hi */
    "\xE3\x83\x95", /* フ fu */
    "\xE3\x83\x98", /* ヘ he */
    "\xE3\x83\x9B", /* ホ ho */
    /* row 3: ma-ra row */
    "\xE3\x83\x9E", /* マ ma */
    "\xE3\x83\x9F", /* ミ mi */
    "\xE3\x83\xA0", /* ム mu */
    "\xE3\x83\xA1", /* メ me */
    "\xE3\x83\xA2", /* モ mo */
    "\xE3\x83\xA4", /* ヤ ya */
    "\xE3\x83\xA6", /* ユ yu */
    "\xE3\x83\xA8", /* ヨ yo */
    "\xE3\x83\xA9", /* ラ ra */
    "\xE3\x83\xAA", /* リ ri */
};

/* Cooking – kitchen tools, ingredients, food, drinks */
static const char *cookingEmojiTable[40] = {
    /* row 0: vessels & condiments */
    "\xF0\x9F\x8D\xB3", /* 🍳 frying pan   */
    "\xF0\x9F\xA5\x98", /* 🥘 shallow pan  */
    "\xF0\x9F\xAB\x95", /* 🫕 fondue       */
    "\xF0\x9F\x8D\xB2", /* 🍲 pot of food  */
    "\xF0\x9F\xA5\x97", /* 🥗 green salad  */
    "\xF0\x9F\xAB\x99", /* 🫙 jar          */
    "\xF0\x9F\xA7\x82", /* 🧂 salt         */
    "\xF0\x9F\xA7\x88", /* 🧈 butter       */
    "\xF0\x9F\xAB\x92", /* 🫒 olive        */
    "\xF0\x9F\x8D\xB1", /* 🍱 bento box    */
    /* row 1: meats, fish, proteins */
    "\xF0\x9F\xA5\xA9", /* 🥩 cut of meat  */
    "\xF0\x9F\x8D\x97", /* 🍗 poultry leg  */
    "\xF0\x9F\x8D\x96", /* 🍖 meat on bone */
    "\xF0\x9F\xA5\x9A", /* 🥚 egg          */
    "\xF0\x9F\xA7\x80", /* 🧀 cheese       */
    "\xF0\x9F\x8D\xA3", /* 🍣 sushi        */
    "\xF0\x9F\x8D\xA4", /* 🍤 fried shrimp */
    "\xF0\x9F\xA6\x90", /* 🦐 shrimp       */
    "\xF0\x9F\xA6\x91", /* 🦑 squid        */
    "\xF0\x9F\xA5\x93", /* 🥓 bacon        */
    /* row 2: breads, pastries, sweets */
    "\xF0\x9F\x8D\x9E", /* 🍞 bread        */
    "\xF0\x9F\xA5\x90", /* 🥐 croissant    */
    "\xF0\x9F\xA5\x96", /* 🥖 baguette     */
    "\xF0\x9F\xA7\x81", /* 🧁 cupcake      */
    "\xF0\x9F\x8E\x82", /* 🎂 birthday cake */
    "\xF0\x9F\x8D\xB0", /* 🍰 shortcake    */
    "\xF0\x9F\x8D\xA9", /* 🍩 doughnut     */
    "\xF0\x9F\x8D\xAA", /* 🍪 cookie       */
    "\xF0\x9F\x8D\xAB", /* 🍫 chocolate    */
    "\xF0\x9F\x8D\xAD", /* 🍭 lollipop     */
    /* row 3: fruits & drinks */
    "\xF0\x9F\x8D\x87", /* 🍇 grapes       */
    "\xF0\x9F\x8D\x93", /* 🍓 strawberry   */
    "\xF0\x9F\x8D\x8E", /* 🍎 red apple    */
    "\xF0\x9F\x8D\x8A", /* 🍊 tangerine    */
    "\xF0\x9F\x8D\x8B", /* 🍋 lemon        */
    "\xE2\x98\x95",     /* ☕ hot beverage */
    "\xF0\x9F\x8D\xB5", /* 🍵 teacup       */
    "\xF0\x9F\xA7\x83", /* 🧃 juice box    */
    "\xF0\x9F\x8D\xBA", /* 🍺 beer mug     */
    "\xF0\x9F\x8D\xB7", /* 🍷 wine glass   */
};

/* Culture – arts, music, landmarks, games, celebrations */
static const char *cultureEmojiTable[40] = {
    /* row 0: performing arts & music */
    "\xF0\x9F\x8E\xAD", /* 🎭 performing arts */
    "\xF0\x9F\x8E\xAC", /* 🎬 clapper board  */
    "\xF0\x9F\x8E\xA4", /* 🎤 microphone     */
    "\xF0\x9F\x8E\xA7", /* 🎧 headphone      */
    "\xF0\x9F\x8E\xB9", /* 🎹 piano          */
    "\xF0\x9F\x8E\xB8", /* 🎸 guitar         */
    "\xF0\x9F\x8E\xBA", /* 🎺 trumpet        */
    "\xF0\x9F\xA5\x81", /* 🥁 drum           */
    "\xF0\x9F\x8E\xBB", /* 🎻 violin         */
    "\xF0\x9F\xAA\x95", /* 🪕 banjo          */
    /* row 1: visual arts, books, circus */
    "\xF0\x9F\x8E\xA8", /* 🎨 artist palette */
    "\xF0\x9F\x96\x8C", /* 🖌 paintbrush     */
    "\xF0\x9F\x93\x9A", /* 📚 books          */
    "\xF0\x9F\x93\x96", /* 📖 open book      */
    "\xF0\x9F\x97\xBF", /* 🗿 moai           */
    "\xF0\x9F\x8F\xBA", /* 🏺 amphora        */
    "\xF0\x9F\x96\xBC", /* 🖼 framed picture */
    "\xF0\x9F\x8E\xAA", /* 🎪 circus tent    */
    "\xF0\x9F\x8E\xA1", /* 🎡 ferris wheel   */
    "\xF0\x9F\x8E\xA2", /* 🎢 roller coaster */
    /* row 2: landmarks & architecture */
    "\xF0\x9F\x8F\xB0", /* 🏰 european castle */
    "\xF0\x9F\x97\xBC", /* 🗼 tokyo tower    */
    "\xF0\x9F\x97\xBD", /* 🗽 statue of liberty */
    "\xE2\x9B\xA9",     /* ⛩ shinto shrine  */
    "\xF0\x9F\x95\x8D", /* 🕍 synagogue      */
    "\xF0\x9F\x8F\xAF", /* 🏯 japanese castle */
    "\xF0\x9F\x8C\x89", /* 🌉 bridge at night */
    "\xF0\x9F\x8E\x87", /* 🎇 sparkler       */
    "\xF0\x9F\x8E\x86", /* 🎆 fireworks      */
    "\xF0\x9F\x8E\x91", /* 🎑 moon ceremony  */
    /* row 3: games & celebrations */
    "\xE2\x99\x9F",     /* ♟ chess pawn     */
    "\xF0\x9F\x8E\xB2", /* 🎲 game die       */
    "\xF0\x9F\x83\x8F", /* 🃏 joker card     */
    "\xF0\x9F\xA7\xA9", /* 🧩 puzzle piece   */
    "\xF0\x9F\x8E\xB0", /* 🎰 slot machine   */
    "\xF0\x9F\x8E\x8A", /* 🎊 confetti ball  */
    "\xF0\x9F\x8E\x89", /* 🎉 party popper   */
    "\xF0\x9F\x8E\x83", /* 🎃 jack-o-lantern */
    "\xF0\x9F\x8E\x84", /* 🎄 christmas tree */
    "\xF0\x9F\xA7\xA7", /* 🧧 red envelope   */
};

/* Year Events – seasonal holidays and celebrations across the calendar */
static const char *yearEventsTable[40] = {
    /* row 0: Christmas & winter */
    "\xF0\x9F\x8E\x84", /* 🎄 christmas tree  */
    "\xF0\x9F\x8E\x85", /* 🎅 santa claus     */
    "\xF0\x9F\xA4\xB6", /* 🤶 mrs. claus      */
    "\xF0\x9F\x8E\x81", /* 🎁 gift            */
    "\xE2\x9B\x84",     /* ⛄ snowman         */
    "\xE2\x9D\x84",     /* ❄ snowflake        */
    "\xF0\x9F\x8C\x9F", /* 🌟 glowing star    */
    "\xF0\x9F\x95\xAF", /* 🕯 candle          */
    "\xF0\x9F\xA7\xA6", /* 🧦 stocking        */
    "\xF0\x9F\x94\x94", /* 🔔 bell            */
    /* row 1: New Year & Valentine */
    "\xF0\x9F\x8E\x86", /* 🎆 fireworks       */
    "\xF0\x9F\x8E\x8A", /* 🎊 confetti ball   */
    "\xF0\x9F\x8E\x89", /* 🎉 party popper    */
    "\xF0\x9F\xA5\x82", /* 🥂 clinking glasses */
    "\xF0\x9F\x8D\xBE", /* 🍾 champagne       */
    "\xE2\x9D\xA4",     /* ❤ heart           */
    "\xF0\x9F\x92\x9D", /* 💝 heart with ribbon */
    "\xF0\x9F\x92\x8C", /* 💌 love letter     */
    "\xF0\x9F\x8C\xB9", /* 🌹 rose            */
    "\xF0\x9F\x92\x98", /* 💘 heart with arrow */
    /* row 2: Easter & Spring */
    "\xF0\x9F\x90\xA3", /* 🐣 hatching chick  */
    "\xF0\x9F\x90\xB0", /* 🐰 rabbit face     */
    "\xF0\x9F\xA5\x9A", /* 🥚 egg             */
    "\xF0\x9F\x8C\xB7", /* 🌷 tulip           */
    "\xF0\x9F\x8C\xB8", /* 🌸 cherry blossom  */
    "\xE2\x98\x98",     /* ☘ shamrock         */
    "\xF0\x9F\x94\xA5", /* 🔥 U+1F525 */
    "\xF0\x9F\xA6\x8B", /* 🦋 butterfly       */
    "\xF0\x9F\x90\x9D", /* 🐝 honeybee        */
    "\xF0\x9F\x8C\xBA", /* 🌺 hibiscus        */
    /* row 3: Halloween & Autumn */
    "\xF0\x9F\x8E\x83", /* 🎃 jack-o-lantern  */
    "\xF0\x9F\x91\xBB", /* 👻 ghost           */
    "\xF0\x9F\xA6\x87", /* 🦇 bat             */
    "\xF0\x9F\x95\xB7", /* 🕷 spider          */
    "\xF0\x9F\x8D\x82", /* 🍂 fallen leaves   */
    "\xF0\x9F\x8D\x81", /* 🍁 maple leaf      */
    "\xF0\x9F\xA6\x83", /* 🦃 turkey          */
    "\xF0\x9F\xAA\x94", /* 🪔 diya lamp       */
    "\xF0\x9F\x95\x8E", /* 🕎 menorah         */
    "\xF0\x9F\x8E\x91", /* 🎑 moon ceremony   */
};

/* Business & Computing – devices, network, office, finance */
static const char *businessEmojiTable[40] = {
    /* row 0: computing devices */
    "\xF0\x9F\x92\xBB", /* 💻 laptop          */
    "\xF0\x9F\x96\xA5", /* 🖥 desktop          */
    "\xF0\x9F\x96\xA8", /* 🖨 printer          */
    "\xE2\x8C\xA8",     /* ⌨  keyboard        */
    "\xF0\x9F\x96\xB1", /* 🖱 mouse            */
    "\xF0\x9F\x92\xBE", /* 💾 floppy disk      */
    "\xF0\x9F\x92\xBF", /* 💿 cd               */
    "\xF0\x9F\x93\xB1", /* 📱 mobile phone     */
    "\xF0\x9F\x96\xB2", /* 🖲 trackball        */
    "\xF0\x9F\x93\xA1", /* 📡 satellite dish   */
    /* row 1: network, security, tech */
    "\xF0\x9F\x8C\x90", /* 🌐 globe w/meridians*/
    "\xF0\x9F\x93\xB6", /* 📶 antenna bars     */
    "\xF0\x9F\x94\x92", /* 🔒 locked           */
    "\xF0\x9F\x94\x93", /* 🔓 unlocked         */
    "\xF0\x9F\x94\x91", /* 🔑 key              */
    "\xE2\x9A\x99",     /* ⚙  gear             */
    "\xF0\x9F\x9B\xA0", /* 🛠 hammer & wrench  */
    "\xF0\x9F\xA4\x96", /* 🤖 robot            */
    "\xF0\x9F\xA7\xA0", /* 🧠 brain            */
    "\xF0\x9F\x92\xA1", /* 💡 light bulb       */
    /* row 2: charts, finance, files */
    "\xF0\x9F\x93\x8A", /* 📊 bar chart        */
    "\xF0\x9F\x93\x88", /* 📈 chart up         */
    "\xF0\x9F\x93\x89", /* 📉 chart down       */
    "\xF0\x9F\x92\xB0", /* 💰 money bag        */
    "\xF0\x9F\x92\xB3", /* 💳 credit card      */
    "\xF0\x9F\x92\xB5", /* 💵 dollar banknote  */
    "\xF0\x9F\x93\x8B", /* 📋 clipboard        */
    "\xF0\x9F\x93\x81", /* 📁 file folder      */
    "\xF0\x9F\x97\x82", /* 🗂 card index       */
    "\xF0\x9F\x8F\xA6", /* 🏦 bank             */
    /* row 3: office, comms, writing */
    "\xF0\x9F\x91\x94", /* 👔 necktie          */
    "\xF0\x9F\x92\xBC", /* 💼 briefcase        */
    "\xF0\x9F\x93\xA7", /* 📧 e-mail           */
    "\xF0\x9F\x93\xA8", /* 📨 incoming envelope*/
    "\xF0\x9F\x93\x85", /* 📅 calendar         */
    "\xE2\x9C\x8F",     /* ✏  pencil           */
    "\xF0\x9F\x93\x9D", /* 📝 memo             */
    "\xF0\x9F\x96\x8A", /* 🖊 pen              */
    "\xF0\x9F\x97\x92", /* 🗒 spiral notepad   */
    "\xF0\x9F\x93\x8C", /* 📌 pushpin          */
};

/* Animals – mammals, birds, sea creatures, reptiles, insects */
static const char *animalsEmojiTable[40] = {
    /* row 0: common mammals */
    "\xF0\x9F\x90\xB6", /* 🐶 dog              */
    "\xF0\x9F\x90\xB1", /* 🐱 cat              */
    "\xF0\x9F\x90\xAD", /* 🐭 mouse            */
    "\xF0\x9F\x90\xB0", /* 🐰 rabbit           */
    "\xF0\x9F\xA6\x8A", /* 🦊 fox              */
    "\xF0\x9F\x90\xBB", /* 🐻 bear             */
    "\xF0\x9F\x90\xBC", /* 🐼 panda            */
    "\xF0\x9F\x90\xA8", /* 🐨 koala            */
    "\xF0\x9F\x90\xAF", /* 🐯 tiger            */
    "\xF0\x9F\xA6\x81", /* 🦁 lion             */
    /* row 1: farm animals & birds */
    "\xF0\x9F\x90\xAE", /* 🐮 cow              */
    "\xF0\x9F\x90\xB7", /* 🐷 pig              */
    "\xF0\x9F\x90\xB8", /* 🐸 frog             */
    "\xF0\x9F\x90\xB5", /* 🐵 monkey           */
    "\xF0\x9F\x99\x88", /* 🙈 see-no-evil      */
    "\xF0\x9F\x90\x94", /* 🐔 chicken          */
    "\xF0\x9F\x90\xA7", /* 🐧 penguin          */
    "\xF0\x9F\x90\xA6", /* 🐦 bird             */
    "\xF0\x9F\xA6\x86", /* 🦆 duck             */
    "\xF0\x9F\xA6\x85", /* 🦅 eagle            */
    /* row 2: sea creatures & insects */
    "\xF0\x9F\x90\xAC", /* 🐬 dolphin          */
    "\xF0\x9F\x90\xB3", /* 🐳 whale            */
    "\xF0\x9F\xA6\x88", /* 🦈 shark            */
    "\xF0\x9F\x90\xA0", /* 🐠 tropical fish    */
    "\xF0\x9F\x90\x99", /* 🐙 octopus          */
    "\xF0\x9F\xA6\x80", /* 🦀 crab             */
    "\xF0\x9F\xA6\x8B", /* 🦋 butterfly        */
    "\xF0\x9F\x90\x9D", /* 🐝 honeybee         */
    "\xF0\x9F\x90\x9B", /* 🐛 bug              */
    "\xF0\x9F\xA6\x97", /* 🦗 cricket          */
    /* row 3: reptiles & exotic animals */
    "\xF0\x9F\x90\x8A", /* 🐊 crocodile        */
    "\xF0\x9F\x90\xA2", /* 🐢 turtle           */
    "\xF0\x9F\xA6\x8E", /* 🦎 lizard           */
    "\xF0\x9F\x90\x8D", /* 🐍 snake            */
    "\xF0\x9F\xA6\x93", /* 🦓 zebra            */
    "\xF0\x9F\xA6\x92", /* 🦒 giraffe          */
    "\xF0\x9F\x90\x98", /* 🐘 elephant         */
    "\xF0\x9F\xA6\x8F", /* 🦏 rhinoceros       */
    "\xF0\x9F\xA6\x94", /* 🦔 hedgehog         */
    "\xF0\x9F\xA6\x98", /* 🦘 kangaroo         */
};

/* -------------------------------------------------------------------------
 * Emoji set descriptor
 * -------------------------------------------------------------------------*/
typedef struct {
    const char  *name;
    const char **emojis; /* points to a [40] array */
} EmojiSetDesc;

static const EmojiSetDesc emojiSets[EMOJIBOX_NUM_SETS] = {
    { "Popular Emojis", popularEmojiTable  },
    { "Urban Emojis",   urbanEmojiTable    },
    { "Nature Emojis",  natureEmojiTable   },
    { "Symbols",        symbolsEmojiTable  },
    { "Sports",         sportsEmojiTable   },
    { "Katakana",       katakanaTable      },
    { "Cooking",        cookingEmojiTable  },
    { "Culture",        cultureEmojiTable  },
    { "Year Events",    yearEventsTable    },
    { "Business",       businessEmojiTable },
    { "Animals",        animalsEmojiTable  },
};

/* =========================================================================
 * Private emoji-grid BOOPSI gadget
 *
 * Renders the 11×5 table (headers + 40 emoji cells) and reports which
 * cell was clicked so the window handler can insert the emoji.
 * =========================================================================
 */

/* Grid cell/header minimum dimensions and window size constants */
#define EGRID_HDR_COL_MIN   72   /* min px for row-label column ("Sh+Ctrl" + pad) */
#define EGRID_HDR_ROW_MIN   14   /* min px for column-label row (small font + 6) */
#define EGRID_CELL_MIN_W    22   /* min px per emoji column (10 cols) */
#define EGRID_CELL_MIN_H    24   /* min px per emoji row    (4 rows)  */
#define EGRID_MIN_GAD_W  (EGRID_HDR_COL_MIN + 10 * EGRID_CELL_MIN_W)
#define EGRID_MIN_GAD_H  (EGRID_HDR_ROW_MIN +  4 * EGRID_CELL_MIN_H)
#define EBOXW_EXTRA_H    130     /* top bar + ANSI section + chrome + spacing */
#define EBOXW_OPEN_W     400
#define EBOXW_OPEN_H     280
#define EBOXW_MIN_W  (EGRID_MIN_GAD_W + 8)
#define EBOXW_MIN_H  (EGRID_MIN_GAD_H + EBOXW_EXTRA_H)

/* Private tag base */
#define EGRID_Dummy      (TAG_USER | 0x7400)
#define EGRID_EmojiSet   (EGRID_Dummy + 1)  /* [IS] const char *[40]        */
#define EGRID_App        (EGRID_Dummy + 2)  /* [IS] struct App *             */
#define EGRID_ClickedIdx (EGRID_Dummy + 3)  /* [G]  last clicked idx (0-39) */

typedef struct {
    const char          **emojis;     /* pointer to 40-entry set  */
    struct App           *egApp;      /* for active-editor access */
    struct URPDrawContext *dc;         /* emoji rendering context  */
    int                   clickedIdx; /* -1 = none                */
    /* Layout metrics filled in GM_RENDER */
    WORD  headerColW;
    WORD  headerRowH;
    WORD  cellW;
    WORD  cellH;
    WORD  fontHeight;
    WORD  fontAscent;
    struct Screen *screen;
    UWORD pens[12]; /* cached copy of dri_Pens, indexed by standard pen constants */
} EmojiGridInst;

/* Safe pen-index aliases that cannot collide with Amiga header macros */
#define EGPEN_BG     BACKGROUNDPEN
#define EGPEN_FILL   FILLPEN
#define EGPEN_SHINE  SHINEPEN
#define EGPEN_SHADOW SHADOWPEN
#define EGPEN_TEXT   TEXTPEN

#define EGRID_INST(cl,o) ((EmojiGridInst *)INST_DATA((cl),(o)))
#define G(o)             ((struct Gadget *)(o))

/* Column and row header strings */
static const char *colLabels[10] = {
    "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10"
};
static const char *rowLabels[4] = {
    "-", "Shift", "Ctrl", "Sh+Ctrl"
};

/* -------------------------------------------------------------------------
 * GM_RENDER
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    EmojiGridInst *inst = EGRID_INST(cl, o);
    struct RastPort *rp  = msg->gpr_RPort;
    struct Gadget   *g   = G(o);
    struct Screen   *scr = msg->gpr_GInfo ? msg->gpr_GInfo->gi_Screen : NULL;
    struct DrawInfo *dri = msg->gpr_GInfo ? msg->gpr_GInfo->gi_DrInfo : NULL;
    WORD gx = g->LeftEdge, gy = g->TopEdge;
    WORD gw = g->Width,    gh = g->Height;
    WORD col, row;
    WORD fontH, baseLine;

    if (!rp || gw <= 0 || gh <= 0) return 0;

    /* Pens */
    if (dri) {
        inst->pens[EGPEN_BG]     = dri->dri_Pens[BACKGROUNDPEN];
        inst->pens[EGPEN_FILL]   = dri->dri_Pens[FILLPEN];
        inst->pens[EGPEN_SHINE]  = dri->dri_Pens[SHINEPEN];
        inst->pens[EGPEN_SHADOW] = dri->dri_Pens[SHADOWPEN];
        inst->pens[EGPEN_TEXT]    = dri->dri_Pens[TEXTPEN];
    }

    /* Emoji font metrics */
    if (scr && inst->dc && scr != inst->screen) {
        struct URPTextMetric m;
        inst->screen = scr;

        URPDC_SetDrawScreen(inst->dc, scr);

        URPDC_GetFontLineMetrics(inst->dc, &m);
        if (m.height <= 0)
        {

            URPDC_TextSizeUTF8(inst->dc, "Agpqj", -1, &m);
        }
        inst->fontHeight = m.height > 0 ? m.height : 14;
        inst->fontAscent = m.baseY  > 0 ? m.baseY  : inst->fontHeight;
    }

    /* System font metrics for header labels */
    fontH    = rp->Font ? (WORD)rp->Font->tf_YSize    : 8;
    baseLine = rp->Font ? (WORD)rp->Font->tf_Baseline : 6;

    /* Layout: header sizes derived from font metrics, equal cell widths/heights */
    inst->headerRowH = fontH + 6;
    {
        /* Measure the widest row label so the column is never too tight */
        WORD maxLblW = 0, ri;
        for (ri = 0; ri < 4; ri++) {
            WORD tw = (WORD)TextLength(rp, rowLabels[ri],
                                       (ULONG)strlen(rowLabels[ri]));
            if (tw > maxLblW) maxLblW = tw;
        }
        inst->headerColW = maxLblW + 14;
        if (inst->headerColW < EGRID_HDR_COL_MIN)
            inst->headerColW = EGRID_HDR_COL_MIN;
    }
    {
        WORD cw = gw - inst->headerColW;
        WORD ch = gh - inst->headerRowH;
        inst->cellW = (cw > 0) ? cw / 10 : 0;
        inst->cellH = (ch > 0) ? ch / 4  : 0;
    }

    /* Full background */
    SetAPen(rp, inst->pens[EGPEN_BG]);
    SetDrMd(rp, JAM1);
    RectFill(rp, (LONG)gx, (LONG)gy,
             (LONG)(gx + gw - 1), (LONG)(gy + gh - 1));

    if (inst->cellW <= 0 || inst->cellH <= 0) return 0;

    /* ---- Header row: F1..F10 column labels ---- */
    for (col = 0; col < 10; col++) {
        WORD cx   = gx + inst->headerColW + col * inst->cellW;
        WORD cw   = inst->cellW;
        WORD ch   = inst->headerRowH;
        const char *lbl = colLabels[col];
        WORD tw, tx, ty;

        SetAPen(rp, inst->pens[EGPEN_SHINE]);
        SetDrMd(rp, JAM1);
        RectFill(rp, (LONG)cx, (LONG)gy,
                 (LONG)(cx + cw - 1), (LONG)(gy + ch - 1));

        tw = (WORD)TextLength(rp, lbl, (ULONG)strlen(lbl));
        tx = cx + (cw - tw) / 2;
        ty = gy + (ch - fontH) / 2 + baseLine;
        SetAPen(rp, inst->pens[EGPEN_SHADOW]);
        SetBPen(rp, inst->pens[EGPEN_SHINE]);
        SetDrMd(rp, JAM2);
        Move(rp, (LONG)tx, (LONG)ty);
        Text(rp, lbl, (ULONG)strlen(lbl));
    }

    /* ---- Header column: row modifier labels ---- */
    for (row = 0; row < 4; row++) {
        WORD rx   = gx;
        WORD ry   = gy + inst->headerRowH + row * inst->cellH;
        WORD rw   = inst->headerColW;
        WORD rh   = inst->cellH;
        const char *lbl = rowLabels[row];
        WORD tw, tx, ty;

        SetAPen(rp, inst->pens[EGPEN_SHINE]);
        SetDrMd(rp, JAM1);
        RectFill(rp, (LONG)rx, (LONG)ry,
                 (LONG)(rx + rw - 1), (LONG)(ry + rh - 1));

        tw = (WORD)TextLength(rp, lbl, (ULONG)strlen(lbl));
        tx = rx + (rw - tw) / 2;
        ty = ry + (rh - fontH) / 2 + baseLine;
        SetAPen(rp, inst->pens[EGPEN_SHADOW]);
        SetBPen(rp, inst->pens[EGPEN_SHINE]);
        SetDrMd(rp, JAM2);
        Move(rp, (LONG)tx, (LONG)ty);
        Text(rp, lbl, (ULONG)strlen(lbl));
    }

    /* ---- Corner cell (top-left) ---- */
    SetAPen(rp, inst->pens[EGPEN_SHINE]);
    SetDrMd(rp, JAM1);
    RectFill(rp, (LONG)gx, (LONG)gy,
             (LONG)(gx + inst->headerColW - 1),
             (LONG)(gy + inst->headerRowH - 1));

    /* ---- Emoji cells ---- */
    if (inst->emojis && inst->dc && inst->screen) {

        URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                  (LONG)inst->pens[EGPEN_TEXT], (LONG)inst->pens[EGPEN_BG]);
        SetAPen(rp, inst->pens[EGPEN_TEXT]);
        SetBPen(rp, inst->pens[EGPEN_BG]);
        SetDrMd(rp, JAM2);

        for (row = 0; row < 4; row++) {
            for (col = 0; col < 10; col++) {
                int idx = row * 10 + col;
                const char *emoji = inst->emojis[idx];
                WORD cx, cy;
                struct URPTextMetric m;
                struct URPTextPos pos;

                if (!emoji || !emoji[0]) continue;

                cx = gx + inst->headerColW + col * inst->cellW;
                cy = gy + inst->headerRowH + row * inst->cellH;

                URPDC_TextSizeUTF8(inst->dc, emoji, -1, &m);
                pos.x = (WORD)(cx + (inst->cellW  - m.width)      / 2);
                pos.y = (WORD)(cy + (inst->cellH  - inst->fontHeight) / 2
                               + inst->fontAscent);
                if (pos.x < cx)                     pos.x = cx;
                if (pos.y < cy + inst->fontAscent)  pos.y = cy + inst->fontAscent;

                URPDrawTextUTF8(rp, inst->dc, &pos, emoji, (ULONG)(-1));
            }
        }
    }

    /* ---- Grid lines (shadow pen) ---- */
    {
        WORD gridRight  = gx + inst->headerColW + 10 * inst->cellW;
        WORD gridBottom = gy + inst->headerRowH + 4  * inst->cellH;

        SetAPen(rp, inst->pens[EGPEN_SHADOW]);
        SetDrMd(rp, JAM1);

        /* Outer border */
        Move(rp, (LONG)gx,        (LONG)gy);
        Draw(rp, (LONG)gridRight,  (LONG)gy);
        Draw(rp, (LONG)gridRight,  (LONG)gridBottom);
        Draw(rp, (LONG)gx,         (LONG)gridBottom);
        Draw(rp, (LONG)gx,         (LONG)gy);

        /* Header/content separator: vertical */
        Move(rp, (LONG)(gx + inst->headerColW), (LONG)gy);
        Draw(rp, (LONG)(gx + inst->headerColW), (LONG)gridBottom);

        /* Header/content separator: horizontal */
        Move(rp, (LONG)gx,        (LONG)(gy + inst->headerRowH));
        Draw(rp, (LONG)gridRight,  (LONG)(gy + inst->headerRowH));

        /* Inner vertical lines (between emoji columns) */
        for (col = 1; col < 10; col++) {
            WORD lx = gx + inst->headerColW + col * inst->cellW;
            Move(rp, (LONG)lx, (LONG)(gy + inst->headerRowH));
            Draw(rp, (LONG)lx, (LONG)gridBottom);
        }

        /* Inner horizontal lines (between emoji rows) */
        for (row = 1; row < 4; row++) {
            WORD ly = gy + inst->headerRowH + row * inst->cellH;
            Move(rp, (LONG)gx,       (LONG)ly);
            Draw(rp, (LONG)gridRight, (LONG)ly);
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * GM_GOACTIVE  – store clicked cell, go active for GADGETUP
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    EmojiGridInst *inst = EGRID_INST(cl, o);
    WORD mx, my, col, row;

    if (!msg->gpi_IEvent) return GMR_NOREUSE;

    /* gpi_Mouse is relative to the gadget's top-left corner */
    mx = msg->gpi_Mouse.X;
    my = msg->gpi_Mouse.Y;

    if (inst->cellW <= 0 || inst->cellH <= 0)  return GMR_NOREUSE;
    if (mx < inst->headerColW)                  return GMR_NOREUSE;
    if (my < inst->headerRowH)                  return GMR_NOREUSE;

    col = (WORD)((mx - inst->headerColW) / inst->cellW);
    row = (WORD)((my - inst->headerRowH) / inst->cellH);
    if (col > 9) col = 9;
    if (row > 3) row = 3;

    inst->clickedIdx = (int)(row * 10 + col);
    *msg->gpi_Termination = 0;
    return GMR_MEACTIVE;
}

/* -------------------------------------------------------------------------
 * GM_HANDLEINPUT  – release triggers GADGETUP
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    struct InputEvent *ie = msg->gpi_IEvent;
    (void)cl; (void)o;
    if (!ie) return GMR_MEACTIVE;
    if (ie->ie_Class == IECLASS_RAWMOUSE) {
        if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
            *msg->gpi_Termination = 0;
            return GMR_NOREUSE | GMR_VERIFY;
        }
        if (ie->ie_Code == IECODE_RBUTTON)
            return GMR_REUSE;
    }
    return GMR_MEACTIVE;
}

/* -------------------------------------------------------------------------
 * GM_GOINACTIVE
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnGoInactive(Class *cl, Object *o,
                                     struct gpGoInactive *msg)
{
    (void)cl; (void)o; (void)msg;
    return 0;
}

/* -------------------------------------------------------------------------
 * OM_NEW
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    EmojiGridInst *inst;
    Object *newObj;
    struct TagItem *ptag;
    ULONG mDispose;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst = EGRID_INST(cl, newObj);
    memset(inst, 0, sizeof(EmojiGridInst));
    inst->clickedIdx = -1;

    ptag = FindTagItem(EGRID_EmojiSet, msg->ops_AttrList);
    if (ptag) inst->emojis = (const char **)ptag->ti_Data;

    ptag = FindTagItem(EGRID_App, msg->ops_AttrList);
    if (ptag) inst->egApp = (struct App *)ptag->ti_Data;

    return (ULONG)newObj;

    (void)mDispose;
}

/* -------------------------------------------------------------------------
 * OM_DISPOSE
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnDispose(Class *cl, Object *o, Msg msg)
{
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* -------------------------------------------------------------------------
 * OM_SET
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    EmojiGridInst *inst = EGRID_INST(cl, o);
    struct TagItem *state = msg->ops_AttrList;
    struct TagItem *tag;
    BOOL redraw = FALSE;

    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag) {
        case EGRID_EmojiSet:
            inst->emojis = (const char **)tag->ti_Data;
            redraw = TRUE;
            break;
        case EGRID_App:
            inst->egApp = (struct App *)tag->ti_Data;
            break;
        default:
            break;
        }
    }

    if (redraw && msg->ops_GInfo) {
        struct RastPort *rp = ObtainGIRPort(msg->ops_GInfo);
        if (rp) {
            DoMethod(o, GM_RENDER, msg->ops_GInfo, rp, GREDRAW_REDRAW);
            ReleaseGIRPort(rp);
        }
    }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* -------------------------------------------------------------------------
 * OM_GET
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnGet(Class *cl, Object *o, struct opGet *msg)
{
    EmojiGridInst *inst = EGRID_INST(cl, o);
    if (msg->opg_AttrID == EGRID_ClickedIdx) {
        *msg->opg_Storage = (ULONG)inst->clickedIdx;
        return TRUE;
    }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

/* -------------------------------------------------------------------------
 * GM_DOMAIN  – report minimum / nominal / maximum dimensions to layout.class
 * -------------------------------------------------------------------------*/
static ULONG EmojiGrid_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    EmojiGridInst *inst    = EGRID_INST(cl, o);
    struct IBox   *domain  = &msg->gpd_Domain;

    /* Use cached metrics from the last GM_RENDER, or fall back to safe estimates */
    WORD hColW   = (inst->headerColW > 0) ? inst->headerColW : EGRID_HDR_COL_MIN;
    WORD hRowH   = (inst->headerRowH > 0) ? inst->headerRowH : EGRID_HDR_ROW_MIN;
    WORD emojiH  = (inst->fontHeight  > 0) ? inst->fontHeight : EGRID_CELL_MIN_H;
    WORD cellHMin = (emojiH + 4 > EGRID_CELL_MIN_H) ? emojiH + 4 : EGRID_CELL_MIN_H;

    domain->Left = 0;
    domain->Top  = 0;

    switch (msg->gpd_Which) {
    case GDOMAIN_MINIMUM:
        domain->Width  = hColW + 10 * EGRID_CELL_MIN_W;
        domain->Height = hRowH +  4 * cellHMin;
        break;
    case GDOMAIN_MAXIMUM:
        domain->Width  = 32767;
        domain->Height = 32767;
        break;
    case GDOMAIN_NOMINAL:
    default:
        domain->Width  = hColW + 10 * (EGRID_CELL_MIN_W * 3);
        domain->Height = hRowH +  4 * (cellHMin * 2);
        break;
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * Dispatcher
 * -------------------------------------------------------------------------*/
static ULONG ASM SAVEDS EmojiGrid_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    switch (msg->MethodID) {
    case OM_NEW:         return EmojiGrid_OnNew(cl, o, (struct opSet *)msg);
    case OM_DISPOSE:     return EmojiGrid_OnDispose(cl, o, msg);
    case OM_SET:
    case OM_UPDATE:      return EmojiGrid_OnSet(cl, o, (struct opSet *)msg);
    case OM_GET:         return EmojiGrid_OnGet(cl, o, (struct opGet *)msg);
    case GM_HITTEST:     return GMR_GADGETHIT;
    case GM_DOMAIN:      return EmojiGrid_OnDomain(cl, o, (struct gpDomain *)msg);
    case GM_RENDER:      return EmojiGrid_OnRender(cl, o, (struct gpRender *)msg);
    case GM_GOACTIVE:    return EmojiGrid_OnGoActive(cl, o, (struct gpInput *)msg);
    case GM_HANDLEINPUT: return EmojiGrid_OnHandleInput(cl, o, (struct gpInput *)msg);
    case GM_GOINACTIVE:  return EmojiGrid_OnGoInactive(cl, o, (struct gpGoInactive *)msg);
    default:             return DoSuperMethodA(cl, o, (APTR)msg);
    }
}

/* =========================================================================
 * EmojiBoxWindow  – window management
 * =========================================================================
 */


/* -------------------------------------------------------------------------
 * EmojiBoxWindow_Init
 * -------------------------------------------------------------------------*/
void EmojiBoxWindow_Init(EmojiBoxWindow *ebw)
{
    LONG sl = ebw->left, st = ebw->top, sw = ebw->width, sh = ebw->height;
    memset(ebw, 0, sizeof(EmojiBoxWindow));
    ebw->left = sl; ebw->top = st; ebw->width = sw; ebw->height = sh;
    NewList(&ebw->chooserList);
}

/* -------------------------------------------------------------------------
 * EmojiBoxWindow_Dispose
 * -------------------------------------------------------------------------*/
void EmojiBoxWindow_Dispose(EmojiBoxWindow *ebw)
{
    int i;
    if (!ebw) return;

    EmojiBoxWindow_Close(ebw);

    if (ebw->windowObj) {
        DisposeObject(ebw->windowObj);
        ebw->windowObj = NULL;
    }

    if (ebw->gridClass) {
        FreeClass(ebw->gridClass);
        ebw->gridClass = NULL;
    }

    if (ebw->dc) {
        URPDC_Release(ebw->dc);
        ebw->dc = NULL;
    }

    /* Free chooser nodes */
    if (ChooserBase) {
        for (i = 0; i < EMOJIBOX_NUM_SETS; i++) {
            if (ebw->chooserNodes[i]) {
                FreeChooserNode(ebw->chooserNodes[i]);
                ebw->chooserNodes[i] = NULL;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * EmojiBoxWindow_Create  (internal – called lazily on first Open)
 * -------------------------------------------------------------------------*/
static BOOL EmojiBoxWindow_Create(EmojiBoxWindow *ebw, struct App *curApp)
{
    Object *topBar;
    Object *setLabel;
    Object *ansiSection;
    Object *styleRow;
    Object *colorRow;
    int i;

    if (ebw->windowObj) return TRUE; /* already created */

    /* Private grid gadget class */
    ebw->gridClass = MakeClass(NULL, "gadgetclass", NULL,
                               sizeof(EmojiGridInst), 0);
    if (!ebw->gridClass) return FALSE;
    ebw->gridClass->cl_Dispatcher.h_Entry = (HOOKFUNC)EmojiGrid_Dispatch;

    /* URPDrawContext for emoji rendering (sized to status bar button dc) */
    if (curApp && curApp->statusBarEmojiBtn)
    {
        ebw->dc = NULL;
        GetAttr(UBT_URPDrawContext, curApp->statusBarEmojiBtn,
                (ULONG *)&ebw->dc);
    }
    if (ebw->dc)
    {
        URPDC_Retain(ebw->dc);
    }
    else
    {
        ebw->dc = URPDC_Create(NULL); /* fallback own context */
    }

    /* Build chooser list (one entry per emoji set) */
    NewList(&ebw->chooserList);
    for (i = 0; i < EMOJIBOX_NUM_SETS; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)emojiSets[i].name,
                                    TAG_END);
        ebw->chooserNodes[i] = node;
        if (node) AddTail(&ebw->chooserList, node);
    }

    /* Set-selector chooser */
    ebw->chooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,             (ULONG)GID_EMOJIBOX_CHOOSER,
        GA_RelVerify,      TRUE,
        CHOOSER_PopUp,     TRUE,
        CHOOSER_Labels,    (ULONG)&ebw->chooserList,
        CHOOSER_Active,   0UL,
        TAG_END);

    /* Label for the chooser */
    setLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)"Set:",
        TAG_END);

    /* Top bar: label + chooser */
    topBar = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_BottomSpacing, 4,
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
                              GA_ReadOnly, TRUE,
                              BUTTON_BevelStyle, BVS_NONE,
                              BUTTON_Transparent, TRUE,
                              TAG_END),
            CHILD_WeightedWidth, 1,
        LAYOUT_AddChild, (ULONG)setLabel,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)ebw->chooser,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth, 180,
        TAG_END);

    /* Grid gadget */
    ebw->gridGadget = (Object *)NewObject(ebw->gridClass, NULL,
        GA_ID,         (ULONG)GID_EMOJIBOX_GRID,
        GA_RelVerify,  TRUE,
        EGRID_EmojiSet,(ULONG)emojiSets[0].emojis,
        EGRID_App,     (ULONG)curApp,
        TAG_END);

    /* Set dc on the grid gadget after construction */
    if (ebw->gridGadget && ebw->dc)
        ((EmojiGridInst *)INST_DATA(ebw->gridClass,
                                    ebw->gridGadget))->dc = ebw->dc;

    /* ANSI escape modifier section – named group (BVS_GROUP) ---------------- */

    styleRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_EvenSize,      TRUE,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_BOLD,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Bold",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_ITALIC,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Italic",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_UNDERLINE,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Underline",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_INVERSE,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Inverse",
            TAG_END),
        TAG_END);

    colorRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_EvenSize,      TRUE,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_BLUE,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Blue",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_RED,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Red",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_GREEN,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Green",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_YELLOW,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Yellow",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_WHITE,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"White",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_BLACK,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Black",
            TAG_END),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        (ULONG)GID_EMOJIBOX_ANSI_NORMAL,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"Normal",
            TAG_END),
        TAG_END);

    ansiSection = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,   BVS_GROUP,
        LAYOUT_Label,        (ULONG)"ANSI Escape Modifiers",
        LAYOUT_SpaceOuter,   TRUE,
        LAYOUT_SpaceInner,   TRUE,
        LAYOUT_AddChild, (ULONG)styleRow,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)colorRow,
            CHILD_WeightedHeight, 0,
        TAG_END);

    /* Main vertical layout: top bar + grid + ANSI section */
    ebw->mainLayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    FALSE,
        LAYOUT_InnerSpacing,  2,
        LAYOUT_LeftSpacing,   2,
        LAYOUT_RightSpacing,  2,
        LAYOUT_TopSpacing,    2,
        LAYOUT_BottomSpacing, 2,
        LAYOUT_AddChild, (ULONG)topBar,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)ebw->gridGadget,
            CHILD_WeightedHeight, 1,
        LAYOUT_AddChild, (ULONG)ansiSection,
            CHILD_WeightedHeight, 0,
        TAG_END);

    if (!ebw->mainLayout) return FALSE;

    /* Window object */
    ebw->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   120,
        WA_Top,    100,
        WA_Width,  EBOXW_OPEN_W,
        WA_Height, EBOXW_OPEN_H,
        WA_MinWidth,  EBOXW_MIN_W,
        WA_MinHeight, EBOXW_MIN_H,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE | IDCMP_RAWKEY,
        WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                   WFLG_SIZEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WA_Title,  (ULONG)"Emoji Box",
        WINDOW_ParentGroup, (ULONG)ebw->mainLayout,
        TAG_END);

    return (ebw->windowObj != NULL);
}

/* -------------------------------------------------------------------------
 * EmojiBoxWindow_Open
 * -------------------------------------------------------------------------*/
void EmojiBoxWindow_Open(EmojiBoxWindow *ebw)
{
    if(!ebw) return;
    if( ebw->window)
    {
        WindowToFront(ebw->window);
        ActivateWindow(ebw->window);
        return;
    }

    if (!ebw->windowObj) {
        /* Lazy creation on first open so we have CurrentMainScreen */
        if (!EmojiBoxWindow_Create(ebw, app)) return;
    }
    if(ebw->windowObj)
    {
        if (CurrentMainScreen)
            SetAttrs(ebw->windowObj,
                     WA_CustomScreen, (ULONG)CurrentMainScreen,
                     TAG_END);

        if (ebw->width > 0) {
            SetAttrs(ebw->windowObj,
                     WA_Left,   (ULONG)ebw->left,
                     WA_Top,    (ULONG)ebw->top,
                     WA_Width,  (ULONG)ebw->width,
                     WA_Height, (ULONG)ebw->height,
                     TAG_END);
        }

        ebw->window = (struct Window *)DoMethod(ebw->windowObj, WM_OPEN, NULL);

    }
}

/* -------------------------------------------------------------------------
 * EmojiBoxWindow_Close
 * -------------------------------------------------------------------------*/
void EmojiBoxWindow_Close(EmojiBoxWindow *ebw)
{
    if (!ebw || !ebw->windowObj || !ebw->window) return;

    GetAttr(WA_Left,   ebw->windowObj, (ULONG *)&ebw->left);
    GetAttr(WA_Top,    ebw->windowObj, (ULONG *)&ebw->top);
    GetAttr(WA_Width,  ebw->windowObj, (ULONG *)&ebw->width);
    GetAttr(WA_Height, ebw->windowObj, (ULONG *)&ebw->height);

    DoMethod(ebw->windowObj, WM_CLOSE, NULL);
    ebw->window = NULL;
}

/* -------------------------------------------------------------------------
 * EmojiBoxWindow_HandleInput
 * -------------------------------------------------------------------------*/
BOOL EmojiBoxWindow_HandleInput(EmojiBoxWindow *ebw)
{
    ULONG result;

    if (!ebw || !ebw->windowObj || !ebw->window) return TRUE;

    while ((result = DoMethod(ebw->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
        case WMHI_CLOSEWINDOW:
            EmojiBoxWindow_Close(ebw);
            return TRUE;
        case WMHI_RAWKEY:
        {
            ULONG key = (result & 0x07f);
            ULONG isUp = (result & 0x080);
            ULONG qualifiers=0;
            GetAttr(WINDOW_Qualifier,ebw->windowObj,&qualifiers);

            /* keys managed at window level */
           // ULONG key = (result & WMHI_KEYMASK);
            if(key == 0x45) {
                EmojiBoxWindow_Close(ebw);
                return TRUE;
            }
            /* send f unction keys to main window */
            if ((key >= EMOJIBOX_RAWKEY_F1 && key <= EMOJIBOX_RAWKEY_F10) && isUp == 0)
            {
                EmojiBox_HandleFKey(app,key,qualifiers,CurrentMainWindow);
            }

        } break;

        case WMHI_GADGETUP:
        {
            ULONG gadId = result & WMHI_GADGETMASK;
            if (gadId == GID_EMOJIBOX_GRID) {
                /* Insert clicked emoji into the active editor */
                ULONG cidx = (ULONG)(-1);
                if (ebw->gridGadget)
                    GetAttr(EGRID_ClickedIdx, ebw->gridGadget, &cidx);
                if (cidx < 40 && app && app->activeEditorObj) {
                    const char *emoji =
                        emojiSets[ebw->currentSetIdx].emojis[cidx];
                    if (emoji)
                        SetGadgetAttrs(app->activeEditorObj,
                                       CurrentMainWindow, NULL,
                                       UTED_InsertText, (ULONG)emoji,
                                       TAG_END);
                }
            } else if (gadId == GID_EMOJIBOX_CHOOSER) {
                /* Switch emoji set */
                ULONG newIdx = 0;
                if (ebw->chooser)
                    GetAttr(CHOOSER_Active, ebw->chooser, &newIdx);
                if ((int)newIdx < EMOJIBOX_NUM_SETS &&
                    (int)newIdx != ebw->currentSetIdx)
                {
                    ebw->currentSetIdx = (int)newIdx;
                    if (ebw->gridGadget && ebw->window)
                        SetGadgetAttrs(
                            (struct Gadget *)ebw->gridGadget,
                            ebw->window, NULL,
                            EGRID_EmojiSet,
                            (ULONG)emojiSets[newIdx].emojis,
                            TAG_END);
                }
            } else if (app && app->activeEditorObj) {
                /* ANSI escape modifier buttons */
                static const struct { ULONG gid; const char *seq; } ansiButtons[] = {
                    { GID_EMOJIBOX_ANSI_BOLD,      "*E[1m"  },
                    { GID_EMOJIBOX_ANSI_ITALIC,    "*E[3m"  },
                    { GID_EMOJIBOX_ANSI_UNDERLINE, "*E[4m"  },
                    { GID_EMOJIBOX_ANSI_INVERSE,   "*E[7m"  },
                    { GID_EMOJIBOX_ANSI_BLUE,      "*E[34m" },
                    { GID_EMOJIBOX_ANSI_RED,       "*E[31m" },
                    { GID_EMOJIBOX_ANSI_GREEN,     "*E[32m" },
                    { GID_EMOJIBOX_ANSI_YELLOW,    "*E[33m" },
                    { GID_EMOJIBOX_ANSI_WHITE,     "*E[37m" },
                    { GID_EMOJIBOX_ANSI_BLACK,     "*E[30m" },
                    { GID_EMOJIBOX_ANSI_NORMAL,    "*E[0m"  },
                    { 0, NULL }
                };
                int ai;
                for (ai = 0; ansiButtons[ai].gid; ai++) {
                    if (gadId == ansiButtons[ai].gid) {
                        SetGadgetAttrs(app->activeEditorObj,
                                       CurrentMainWindow, NULL,
                                       UTED_InsertText, (ULONG)ansiButtons[ai].seq,
                                       TAG_END);
                        break;
                    }
                }
            }
            break;
        }

        default:
            break;
        }
    }
    return TRUE;
}

/* -------------------------------------------------------------------------
 * EmojiBoxWindow_GetSignalMask
 * -------------------------------------------------------------------------*/
ULONG EmojiBoxWindow_GetSignalMask(EmojiBoxWindow *ebw)
{
    if (!ebw || !ebw->window) return 0;
    return (1UL << ebw->window->UserPort->mp_SigBit);
}

/* =========================================================================
 * EmojiBox_HandleFKey  – inserts from the currently selected emoji set.
 * =========================================================================
 */
BOOL EmojiBox_HandleFKey(struct App *ctx, ULONG code, ULONG qualifiers,
                         struct Window *win)
{
    ULONG idx;
    int setIdx;
    const char *emoji;

    if (code < EMOJIBOX_RAWKEY_F1 || code > EMOJIBOX_RAWKEY_F10)
        return FALSE;
    if (!ctx || !ctx->activeEditorObj)
        return FALSE;

    idx = code - EMOJIBOX_RAWKEY_F1;
    if (qualifiers & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) idx += 10;
    if (qualifiers & IEQUALIFIER_CONTROL)                        idx += 20;

    setIdx = ctx->emojiBoxWindow.currentSetIdx;
    if (setIdx < 0 || setIdx >= EMOJIBOX_NUM_SETS) setIdx = 0;
    emoji = emojiSets[setIdx].emojis[idx];
    if (!emoji) return FALSE;

    SetGadgetAttrs(ctx->activeEditorObj, win, NULL,
                   UTED_InsertText, (ULONG)emoji,
                   TAG_END);
    return TRUE;
}
