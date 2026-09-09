/* Ersatz fuer den von ESP-IDF generierten sdkconfig.h.
 *
 * main/ui_fonts.h ist die einzige Datei im UI-Layer, die sdkconfig.h ueberhaupt
 * braucht, und sie fragt genau ein Symbol ab. Auf dem Geraet kommt es aus
 * main/Kconfig.projbuild (default y), hier steht es fest: der Simulator soll die
 * echten Inter-Faces aus main/fonts/ zeigen, nicht den Montserrat-Fallback.
 */
#ifndef SIM_SDKCONFIG_H
#define SIM_SDKCONFIG_H

#define CONFIG_WEATHER_USE_INTER_FONTS 1

#endif
