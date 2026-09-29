#ifndef CONFIG_H
#define CONFIG_H

// Animation interpolation factor (higher = faster/snappier, lower = smoother/slower)
static const float anim_step = 0.15f;

// Main loop sleep time in microseconds (16000us ≈ 60 FPS)
static const useconds_t idle_sleep_us = 16000;

// Window classes to ignore/skip rendering
static const char *ignore_classes[] = {
    "conky",
    "polybar",
    NULL
};

#endif
