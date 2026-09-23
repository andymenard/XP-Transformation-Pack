#include "schema.h"

#include <initializer_list>

namespace xpl::schema {
namespace {

struct Part
{
    const char *name;
    int id;
    const char *const *states;  // state names in id order (index 0 is id 1), nullptr-terminated
};

struct Class
{
    const char *name;
    std::initializer_list<Part> parts;
};

// State lists, named as tmschema.h does (lower-case, without the prefix).
const char *const kButton5[] = {"normal", "hot", "pressed", "disabled", "defaulted", nullptr};
const char *const kRadio[] = {"uncheckednormal", "uncheckedhot", "uncheckedpressed", "uncheckeddisabled",
                              "checkednormal", "checkedhot", "checkedpressed", "checkeddisabled", nullptr};
const char *const kCheck[] = {"uncheckednormal", "uncheckedhot", "uncheckedpressed", "uncheckeddisabled",
                              "checkednormal", "checkedhot", "checkedpressed", "checkeddisabled",
                              "mixednormal", "mixedhot", "mixedpressed", "mixeddisabled", nullptr};
const char *const kGroupBox[] = {"normal", "disabled", nullptr};
const char *const kFour[] = {"normal", "hot", "pressed", "disabled", nullptr};
const char *const kThree[] = {"normal", "hot", "pressed", nullptr};
const char *const kToolbar[] = {"normal", "hot", "pressed", "disabled", "checked", "hotchecked", nullptr};
const char *const kTab[] = {"normal", "hot", "selected", "disabled", "focused", nullptr};
const char *const kThumb[] = {"normal", "hot", "pressed", "focused", "disabled", nullptr};
const char *const kNormal[] = {"normal", nullptr};
const char *const kArrow[] = {"upnormal", "uphot", "uppressed", "updisabled",
                              "downnormal", "downhot", "downpressed", "downdisabled",
                              "leftnormal", "lefthot", "leftpressed", "leftdisabled",
                              "rightnormal", "righthot", "rightpressed", "rightdisabled", nullptr};
const char *const kSizeBox[] = {"rightalign", "leftalign", nullptr};
const char *const kEdit[] = {"normal", "hot", "selected", "disabled", "focused", "readonly", "assist", nullptr};
const char *const kListItem[] = {"normal", "hot", "selected", "disabled", "selectednotfocus", nullptr};
const char *const kGlyph[] = {"closed", "opened", nullptr};
const char *const kSort[] = {"sortedup", "sorteddown", nullptr};
const char *const kMenu[] = {"normal", "selected", "demoted", nullptr};
const char *const kTooltip[] = {"normal", "link", nullptr};
const char *const kFrame[] = {"active", "inactive", nullptr};
const char *const kCaption[] = {"active", "inactive", "disabled", nullptr};
const char *const kCaptionButton[] = {"normal", "hot", "pushed", "disabled", nullptr};
const char *const kPin[] = {"normal", "hot", "pressed", "selectednormal", "selectedhot", "selectedpressed", nullptr};

const Class kClasses[] = {
    {"button", {{"pushbutton", 1, kButton5}, {"radiobutton", 2, kRadio}, {"checkbox", 3, kCheck},
                {"groupbox", 4, kGroupBox}, {"userbutton", 5, kFour}}},
    {"rebar", {{"gripper", 1, nullptr}, {"grippervert", 2, nullptr}, {"band", 3, nullptr},
               {"chevron", 4, kThree}, {"chevronvert", 5, kThree}}},
    {"toolbar", {{"button", 1, kToolbar}, {"dropdownbutton", 2, kToolbar}, {"splitbutton", 3, kToolbar},
                 {"splitbuttondropdown", 4, kToolbar}, {"separator", 5, kToolbar}, {"separatorvert", 6, kToolbar}}},
    {"status", {{"pane", 1, nullptr}, {"gripperpane", 2, nullptr}, {"gripper", 3, nullptr}}},
    {"menu", {{"menuitem", 1, kMenu}, {"menudropdown", 2, kMenu}, {"menubaritem", 3, kMenu},
              {"menubardropdown", 4, kMenu}, {"chevron", 5, kMenu}, {"separator", 6, kMenu}}},
    {"listview", {{"listitem", 1, kListItem}, {"listgroup", 2, nullptr}, {"listdetail", 3, nullptr},
                  {"listsorteddetail", 4, nullptr}, {"emptytext", 5, nullptr}}},
    {"header", {{"headeritem", 1, kThree}, {"headeritemleft", 2, kThree}, {"headeritemright", 3, kThree},
                {"headersortarrow", 4, kSort}}},
    {"progress", {{"bar", 1, nullptr}, {"barvert", 2, nullptr}, {"chunk", 3, nullptr}, {"chunkvert", 4, nullptr}}},
    {"tab", {{"tabitem", 1, kTab}, {"tabitemleftedge", 2, kTab}, {"tabitemrightedge", 3, kTab},
             {"tabitembothedge", 4, kTab}, {"toptabitem", 5, kTab}, {"toptabitemleftedge", 6, kTab},
             {"toptabitemrightedge", 7, kTab}, {"toptabitembothedge", 8, kTab}, {"pane", 9, nullptr},
             {"body", 10, nullptr}}},
    {"trackbar", {{"track", 1, kNormal}, {"trackvert", 2, kNormal}, {"thumb", 3, kThumb},
                  {"thumbbottom", 4, kThumb}, {"thumbtop", 5, kThumb}, {"thumbvert", 6, kThumb},
                  {"thumbleft", 7, kThumb}, {"thumbright", 8, kThumb}, {"tics", 9, kNormal},
                  {"ticsvert", 10, kNormal}}},
    {"tooltip", {{"standard", 1, kTooltip}, {"standardtitle", 2, kTooltip}, {"balloon", 3, kTooltip},
                 {"balloontitle", 4, kTooltip}, {"close", 5, kThree}}},
    {"treeview", {{"treeitem", 1, kListItem}, {"glyph", 2, kGlyph}, {"branch", 3, nullptr}}},
    {"spin", {{"up", 1, kFour}, {"down", 2, kFour}, {"uphorz", 3, kFour}, {"downhorz", 4, kFour}}},
    {"page", {{"up", 1, kFour}, {"down", 2, kFour}, {"uphorz", 3, kFour}, {"downhorz", 4, kFour}}},
    {"scrollbar", {{"arrowbtn", 1, kArrow}, {"thumbbtnhorz", 2, kFour}, {"thumbbtnvert", 3, kFour},
                   {"lowertrackhorz", 4, kFour}, {"uppertrackhorz", 5, kFour}, {"lowertrackvert", 6, kFour},
                   {"uppertrackvert", 7, kFour}, {"gripperhorz", 8, kFour}, {"grippervert", 9, kFour},
                   {"sizebox", 10, kSizeBox}}},
    {"edit", {{"edittext", 1, kEdit}, {"caret", 2, nullptr}}},
    {"combobox", {{"dropdownbutton", 1, kFour}}},
    {"clock", {{"time", 1, kNormal}}},
    {"traynotify", {{"background", 1, nullptr}, {"animbackground", 2, nullptr}}},
    {"taskbar", {{"backgroundbottom", 1, nullptr}, {"backgroundright", 2, nullptr}, {"backgroundtop", 3, nullptr},
                 {"backgroundleft", 4, nullptr}, {"sizingbarbottom", 5, nullptr}, {"sizingbarright", 6, nullptr},
                 {"sizingbartop", 7, nullptr}, {"sizingbarleft", 8, nullptr}}},
    {"taskband", {{"groupcount", 1, nullptr}, {"flashbutton", 2, nullptr}, {"flashbuttongroupmenu", 3, nullptr}}},
    {"startpanel", {{"userpane", 1, nullptr}, {"moreprograms", 2, nullptr}, {"moreprogramsarrow", 3, kThree},
                    {"proglist", 4, nullptr}, {"proglistseparator", 5, nullptr}, {"placeslist", 6, nullptr},
                    {"placeslistseparator", 7, nullptr}, {"logoff", 8, nullptr}, {"logoffbuttons", 9, kThree},
                    {"userpicture", 10, nullptr}, {"preview", 11, nullptr}}},
    {"explorerbar", {{"headerbackground", 1, nullptr}, {"headerclose", 2, kThree}, {"headerpin", 3, kPin},
                     {"iebarmenu", 4, kThree}, {"normalgroupbackground", 5, nullptr},
                     {"normalgroupcollapse", 6, kThree}, {"normalgroupexpand", 7, kThree},
                     {"normalgrouphead", 8, nullptr}, {"specialgroupbackground", 9, nullptr},
                     {"specialgroupcollapse", 10, kThree}, {"specialgroupexpand", 11, kThree},
                     {"specialgrouphead", 12, nullptr}}},
    {"menuband", {{"newappbutton", 1, kToolbar}, {"seperator", 2, nullptr}}},   // sic: tmschema spells it so
    {"window", {{"caption", 1, kCaption}, {"smallcaption", 2, kCaption}, {"mincaption", 3, kCaption},
                {"smallmincaption", 4, kCaption}, {"maxcaption", 5, kCaption}, {"smallmaxcaption", 6, kCaption},
                {"frameleft", 7, kFrame}, {"frameright", 8, kFrame}, {"framebottom", 9, kFrame},
                {"smallframeleft", 10, kFrame}, {"smallframeright", 11, kFrame}, {"smallframebottom", 12, kFrame},
                {"sysbutton", 13, kCaptionButton}, {"mdisysbutton", 14, kCaptionButton},
                {"minbutton", 15, kCaptionButton}, {"mdiminbutton", 16, kCaptionButton},
                {"maxbutton", 17, kCaptionButton}, {"closebutton", 18, kCaptionButton},
                {"smallclosebutton", 19, kCaptionButton}, {"mdiclosebutton", 20, kCaptionButton},
                {"restorebutton", 21, kCaptionButton}, {"mdirestorebutton", 22, kCaptionButton},
                {"helpbutton", 23, kCaptionButton}, {"mdihelpbutton", 24, kCaptionButton},
                {"horzscroll", 25, kCaptionButton}, {"horzthumb", 26, kCaptionButton},
                {"vertscroll", 27, kCaptionButton}, {"vertthumb", 28, kCaptionButton}, {"dialog", 29, nullptr},
                {"captionsizingtemplate", 30, nullptr}, {"smallcaptionsizingtemplate", 31, nullptr},
                {"frameleftsizingtemplate", 32, nullptr}, {"smallframeleftsizingtemplate", 33, nullptr},
                {"framerightsizingtemplate", 34, nullptr}, {"smallframerightsizingtemplate", 35, nullptr},
                {"framebottomsizingtemplate", 36, nullptr}, {"smallframebottomsizingtemplate", 37, nullptr}}},
};

bool same(QStringView a, const char *b)
{
    return a.compare(QLatin1String(b), Qt::CaseInsensitive) == 0;
}

const Class *findClass(QStringView cls)
{
    for (const Class &c : kClasses)
        if (same(cls, c.name))
            return &c;
    return nullptr;
}

const Part *findPart(QStringView cls, QStringView part)
{
    if (const Class *c = findClass(cls))
        for (const Part &p : c->parts)
            if (same(part, p.name))
                return &p;
    return nullptr;
}

} // namespace

int partId(QStringView cls, QStringView part)
{
    const Part *p = findPart(cls, part);
    return p ? p->id : 0;
}

int stateId(QStringView cls, QStringView part, QStringView state)
{
    const Part *p = findPart(cls, part);
    if (!p || !p->states)
        return 0;
    for (int i = 0; p->states[i]; ++i)
        if (same(state, p->states[i]))
            return i + 1;
    return 0;
}

QString partName(QStringView cls, int part)
{
    if (const Class *c = findClass(cls))
        for (const Part &p : c->parts)
            if (p.id == part)
                return QLatin1String(p.name);
    return QString();
}

QString stateName(QStringView cls, QStringView part, int state)
{
    const Part *p = findPart(cls, part);
    if (!p || !p->states || state < 1)
        return QString();
    for (int i = 0; p->states[i]; ++i)
        if (i + 1 == state)
            return QLatin1String(p->states[i]);
    return QString();
}

} // namespace xpl::schema
