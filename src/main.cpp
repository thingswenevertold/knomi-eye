#include <Arduino.h>
#include "display/display.h"
#include "ui/face.h"
#include "ui/tuning.h"
#include "ui/mood.h"
#include "ui/status.h"
#include "input/button.h"
#include "ota.h"
#include "web/admin_server.h"
#include "net/ble.h"
#include "net/wifiprov.h"
#include "diag.h"

namespace {
    enum class Screen { Face, Status };
    Screen screen = Screen::Face;
    int statusPage = 0;
    uint32_t statusUntilMs = 0;
    bool adminActive = false;
}

void setup() {
    display::begin();
    tuning::begin();   // owns the backlight; must precede the first palette read
    face::begin();
    status::begin();
    button::begin();

    // BLE first: it is the only control surface that works with no network
    // at all, so it must come up even if the WiFi association below fails —
    // and it is how a new network gets provisioned in the first place.
    wifiprov::begin();
    ble::begin();

    ota::begin();
    if (ota::isConnected()) {
        admin::begin();
        adminActive = true;
    }
}

void loop() {
    uint32_t now = millis();

    switch (button::poll(now)) {
        case button::Event::Click:
            diag::setButtonEvent("click");
            face::notifyInteraction(now);
            if (screen == Screen::Face) {
                screen = Screen::Status;
                statusPage = 0;
            } else {
                statusPage = (statusPage + 1) % status::pageCount();
            }
            statusUntilMs = now + 10000;
            break;
        case button::Event::LongPress:
            diag::setButtonEvent("long_press");
            face::notifyInteraction(now);
            face::triggerSpecial();
            break;
        case button::Event::None:
            break;
    }

    // WiFi can arrive long after boot, when a network is provisioned over
    // Bluetooth. Starting the web services only in setup() meant a device
    // that booted without a network never served its dashboard at all.
    if (!adminActive && ota::isConnected()) {
        ota::startServices();
        admin::begin();
        adminActive = true;
    }

    if (screen == Screen::Status && now >= statusUntilMs) {
        screen = Screen::Face;
    }

    diag::setScreen(screen == Screen::Face ? "face" : "status");

    const uint32_t tDraw0 = micros();
    if (screen == Screen::Face) {
        face::update(now);
    } else {
        status::update(now, statusPage);
    }
    const uint32_t tDraw1 = micros();

    // Le lien BLE est une presence au meme titre que le poste allume.
    mood::setBleLinked(ble::isConnected());
    ble::handle(now);
    const uint32_t tBle = micros();
    ota::handle();
    const uint32_t tOta = micros();
    if (adminActive) {
        admin::handle(now);
    }
    const uint32_t tNet1 = micros();
    diag::setNetSplit(tBle - tDraw1, tOta - tBle, tNet1 - tOta);
    diag::setTimings(tDraw1 - tDraw0, tNet1 - tDraw1, tNet1 - tDraw0);
    // Cadence sur la duree reelle de la frame, au lieu d'ajouter 16 ms a
    // celle-ci : le rendu coute deja plusieurs millisecondes, donc le "~60 fps"
    // annonce n'etait jamais atteint, et l'intervalle variait avec la charge.
    // On garde toujours au moins un delay(1) pour que WiFi et BLE aient leur
    // tour — les affamer coute bien plus cher que quelques images.
    // Cadence observee, lissee sur environ une seconde. Mesuree avant le
    // delai d'attente, donc c'est bien le cout reel d'une image.
    {
        static uint32_t lastFrameMs = 0;
        static float smoothed = 0.0f;
        const uint32_t end = millis();
        if (lastFrameMs != 0) {
            const uint32_t d = end - lastFrameMs;
            if (d > 0) {
                const float inst = 1000.0f / (float)d;
                smoothed = (smoothed == 0.0f) ? inst : (smoothed * 0.94f + inst * 0.06f);
                diag::setFps(smoothed);
            }
        }
        lastFrameMs = end;
    }

    // 40 ms, soit 25 images par seconde regulieres.
    //
    // Viser plus haut ne servait a rien : le dessin coute 32 a 35 ms mesures,
    // donc la condition ci-dessous etait toujours vraie et il n'y avait en
    // pratique aucune regulation — l'intervalle suivait le temps de dessin et
    // sautillait avec lui. Un intervalle stable un peu plus long se voit
    // beaucoup mieux qu'un intervalle nerveux un peu plus court.
    //
    // Cette valeur est aussi celle du frameMs de l'art ASCII, pour qu'une
    // frame d'art dure exactement une image rendue. Les deux doivent bouger
    // ensemble : c'est leur rapport, et non leur valeur, qui supprime les
    // sauts.
    const uint32_t FRAME_MS = 40;
    const uint32_t spent = millis() - now;
    delay(spent >= FRAME_MS ? 1 : FRAME_MS - spent);
}
