// BACKEND ABSTRACTION LAYER
// SDL3/OpenGL backend for Cro-Mag Rally
// (C) 2025 Migration Project
// This file is part of Cro-Mag Rally. https://github.com/jorio/CroMagRally

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

//==============================================================================
// Include SDL3 headers
//==============================================================================

#include <SDL3/SDL.h>

//==============================================================================
// Common Types
//==============================================================================

// Key/Scancode type - maps to platform-specific codes
typedef int32_t BackendKey;

// Gamepad handle
typedef struct BackendGamepad BackendGamepad;

// Window handle
typedef struct BackendWindow BackendWindow;

//==============================================================================
// Backend Initialization
//==============================================================================

// Initialize the backend (call once at startup)
bool Backend_Init(void);

// Shutdown the backend (call once at exit)
void Backend_Shutdown(void);

// Get backend name for logging
const char* Backend_GetName(void);

#ifdef __cplusplus
}
#endif