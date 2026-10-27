// N70 temporary test harness: PB_TOUCH_TEST=<file> pushes SDL finger/key events at UI frames and logs port 1.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <typeinfo>
extern "C" {
extern u16 HuPadBtn[4];
extern s8 HuPadStkX[4];
extern s8 HuPadStkY[4];
}
namespace {
struct TestEv { unsigned frame; char type[8]; unsigned id; float x, y; };
std::vector<TestEv> sTestEvs;
size_t sTestNext = 0;
unsigned sTestFrame = 0;
bool sTestLoaded = false;
void touch_test_tick()
{
    if (!sTestLoaded) {
        sTestLoaded = true;
        if (const char *path = getenv("PB_TOUCH_TEST")) {
            if (FILE *f = fopen(path, "r")) {
                TestEv e {};
                while (fscanf(f, "%u %7s %u %f %f", &e.frame, e.type, &e.id, &e.x, &e.y) == 5) {
                    sTestEvs.push_back(e);
                }
                fclose(f);
            }
            fprintf(stderr, "touchtest: %zu events\n", sTestEvs.size());
        }
    }
    sTestFrame++;
    if (sTestEvs.empty()) {
        return;
    }
    static u16 lastBtn = 0xFFFF;
    static s8 lastX = 99, lastY = 99;
    static bool lastShown = false;
    if (HuPadBtn[0] != lastBtn || HuPadStkX[0] != lastX || HuPadStkY[0] != lastY || touch_pad_shown() != lastShown) {
        fprintf(stderr, "touchtest: frame %u shown %d live %d HuPadBtn %04X stick %d,%d virt btn %04X\n", sTestFrame,
            touch_pad_shown(), live(), HuPadBtn[0], HuPadStkX[0], HuPadStkY[0], held_buttons());
        if (Document *d = top_document()) fprintf(stderr, "touchtest: top %s visible %d\n", typeid(*d).name(), d->visible());
        lastBtn = HuPadBtn[0]; lastX = HuPadStkX[0]; lastY = HuPadStkY[0]; lastShown = touch_pad_shown();
    }
    while (sTestNext < sTestEvs.size() && sTestEvs[sTestNext].frame <= sTestFrame) {
        const TestEv &e = sTestEvs[sTestNext++];
        SDL_Event ev {};
        if (strcmp(e.type, "key") == 0) {
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.scancode = SDL_SCANCODE_SPACE;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_KEY_UP;
        } else {
            ev.type = strcmp(e.type, "down") == 0 ? SDL_EVENT_FINGER_DOWN
                : strcmp(e.type, "move") == 0     ? SDL_EVENT_FINGER_MOTION
                                                  : SDL_EVENT_FINGER_UP;
            ev.tfinger.touchID = 1;
            ev.tfinger.fingerID = e.id;
            ev.tfinger.x = e.x;
            ev.tfinger.y = e.y;
        }
        SDL_PushEvent(&ev);
        fprintf(stderr, "touchtest: frame %u push %s %u %.3f %.3f\n", sTestFrame, e.type, e.id, e.x, e.y);
    }
}
}
