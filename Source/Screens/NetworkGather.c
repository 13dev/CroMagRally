/****************************/
/* NETWORK GATHER.C         */
/* Room code multiplayer    */
/* (C) 2026                 */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "network.h"


/****************************/
/*    PROTOTYPES            */
/****************************/

static void SetupNetworkGatherScreen(bool isHost);
static int DoNetworkHostControls(void);
static int DoNetworkJoinControls(void);


/****************************/
/*    CONSTANTS             */
/****************************/

#define ROOM_CODE_LENGTH        4   // "ABCD"
#define PLAYER_NAME_MAX_LENGTH  16


/*********************/
/*    VARIABLES      */
/*********************/

static ObjNode* gTitleText = NULL;
static ObjNode* gStatusText = NULL;
static ObjNode* gRoomCodeText = NULL;
static ObjNode* gPlayerCountText = NULL;
static ObjNode* gPlayerListText[NET_MAX_PLAYERS];
static ObjNode* gBackPrompt = NULL;
static ObjNode* gStartPrompt = NULL;
static ObjNode* gRoomCodeInputText = NULL;
static ObjNode* gNameInputText = NULL;
static ObjNode* gNamePromptText = NULL;

// Room code input for joining
static char     gRoomCodeInput[ROOM_CODE_LENGTH + 1] = "";
static int      gRoomCodeCursor = 0;

// Player name input
static char     gPlayerNameInput[PLAYER_NAME_MAX_LENGTH + 1] = "";
static int      gPlayerNameCursor = 0;
static bool     gNameInputComplete = false;


/********************* SETUP NETWORK GATHER SCREEN **********************/

static void SetupNetworkGatherScreen(bool isHost)
{
    OGLSetupInputType   viewDef;
    OGLColorRGBA        ambientColor = { .5, .5, .5, 1 };
    OGLColorRGBA        fillColor1 = { 1.0, 1.0, 1.0, 1 };
    OGLVector3D         fillDirection1 = { .9, -.3, -1 };

            /**************/
            /* SETUP VIEW */
            /**************/

    OGL_NewViewDef(&viewDef);

    viewDef.camera.fov              = .3;
    viewDef.camera.hither           = 10;
    viewDef.camera.yon              = 3000;
    viewDef.camera.from[0].z        = 700;

    viewDef.view.clearColor         = (OGLColorRGBA) { 0, 0, 0, 1 };
    viewDef.styles.useFog           = false;
    viewDef.view.pillarboxRatio     = PILLARBOX_RATIO_4_3;

    viewDef.lights.ambientColor     = ambientColor;
    viewDef.lights.numFillLights    = 1;
    viewDef.lights.fillDirection[0] = fillDirection1;
    viewDef.lights.fillColor[0]     = fillColor1;

    viewDef.view.fontName           = "rockfont";

    OGL_SetupGameView(&viewDef);

    // Initialize notifications for player join/disconnect messages
    InitNotifications();


                /************/
                /* LOAD ART */
                /************/

    MakeScrollingBackgroundPattern();


            /*****************/
            /* BUILD OBJECTS */
            /*****************/

    // Title
    NewObjectDefinitionType def =
    {
        .scale = 0.5f,
        .coord = {0, -180, 0},
        .slot = SPRITE_SLOT,
    };

    const char* title = isHost ? Localize(STR_HOST_NET_GAME) : Localize(STR_JOIN_NET_GAME);
    gTitleText = TextMesh_New(title, 0, &def);
    gTitleText->ColorFilter = (OGLColorRGBA) {1, .8f, .2f, 1};

    // Status text
    def.coord.y = -130;
    def.scale = 0.3f;
    gStatusText = TextMesh_NewEmpty(128, &def);

    // Room code display (large, prominent)
    def.coord.y = -60;
    def.scale = 0.6f;
    gRoomCodeText = TextMesh_NewEmpty(16, &def);
    gRoomCodeText->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};

    // Player count
    def.coord.y = 10;
    def.scale = 0.25f;
    gPlayerCountText = TextMesh_NewEmpty(64, &def);

    // Player list (reduced spacing: 22px instead of 30px)
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        def.coord.y = 45 + i * 22;
        def.scale = 0.22f;
        gPlayerListText[i] = TextMesh_NewEmpty(48, &def);
        gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {.8f, .8f, .8f, 1};
    }

    // Back prompt
    def.coord.y = 190;
    def.scale = 0.22f;
    gBackPrompt = TextMesh_New(Localize(STR_PRESS_ESC_TO_GO_BACK), 0, &def);
    gBackPrompt->ColorFilter = (OGLColorRGBA) {.5, .5, .5, 1};
    MakeTwitch(gBackPrompt, kTwitchPreset_PressKeyPrompt);

    // Start game prompt (host only, hidden initially)
    if (isHost)
    {
        def.coord.y = 220;
        def.scale = 0.25f;
        gStartPrompt = TextMesh_New("[ENTER] START GAME", 0, &def);
        gStartPrompt->ColorFilter = (OGLColorRGBA) {.3f, .3f, .3f, 1};  // Dim until 2+ players
    }
    else
    {
        gStartPrompt = NULL;
    }

    // Room code input (join screen only)
    if (!isHost)
    {
        def.coord.y = -30;
        def.scale = 0.6f;
        gRoomCodeInputText = TextMesh_NewEmpty(16, &def);
        gRoomCodeInputText->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};

        // Hide the room code display text (we'll use input text instead)
        gRoomCodeText->StatusBits |= STATUS_BIT_HIDDEN;
    }
    else
    {
        gRoomCodeInputText = NULL;
    }

    // Name input prompt (shown initially before host/join UI)
    def.coord.y = -80;
    def.scale = 0.35f;
    gNamePromptText = TextMesh_New("ENTER YOUR NAME:", 0, &def);
    gNamePromptText->ColorFilter = (OGLColorRGBA) {1, 1, 1, 1};

    // Name input text
    def.coord.y = -30;
    def.scale = 0.5f;
    gNameInputText = TextMesh_NewEmpty(PLAYER_NAME_MAX_LENGTH + 4, &def);
    gNameInputText->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};

    // Reset inputs
    gRoomCodeInput[0] = '\0';
    gRoomCodeCursor = 0;

    // Initialize player name - start empty so user types their name
    gPlayerNameInput[0] = '\0';
    gPlayerNameCursor = 0;
    gNameInputComplete = false;
}


/********************* UPDATE PLAYER LIST (SHARED) **********************/
//
// Shared function to display the player list for both host and client
//

static void UpdatePlayerList(bool isHost)
{
    int numPlayers = HostGetGatheredPlayerCount();  // Works for both host and client
    int myPlayerIndex = Net_GetLocalPlayerIndex();

    // Debug: print once when we have valid data
    static bool debugPrinted = false;
    if (!debugPrinted && numPlayers > 0)
    {
        printf("[DEBUG] UpdatePlayerList: isHost=%d, numPlayers=%d, myPlayerIndex=%d\n",
               isHost, numPlayers, myPlayerIndex);
        for (int d = 0; d < NET_MAX_PLAYERS; d++)
        {
            printf("[DEBUG]   Player %d name: '%s'\n", d, gPlayerNameStrings[d]);
        }
        fflush(stdout);
        debugPrinted = true;
    }

    // Update player list
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        if (i < numPlayers)
        {
            char nameStr[48];
            if (i == 0)
            {
                // Player 0 is always the host
                if (isHost)
                {
                    // We are the host
                    SDL_snprintf(nameStr, sizeof(nameStr), "%d. %s (YOU)",
                                i + 1, gPlayerNameStrings[i]);
                    gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};  // Green for self
                }
                else
                {
                    // We are client, show host
                    SDL_snprintf(nameStr, sizeof(nameStr), "%d. %s (HOST)",
                                i + 1, gPlayerNameStrings[i]);
                    gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {1, 1, .5f, 1};  // Yellow for host
                }
            }
            else if (i == myPlayerIndex)
            {
                // This is us (client)
                SDL_snprintf(nameStr, sizeof(nameStr), "%d. %s (YOU)",
                            i + 1, gPlayerNameStrings[i]);
                gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};  // Green for self
            }
            else
            {
                // Other player
                SDL_snprintf(nameStr, sizeof(nameStr), "%d. %s",
                            i + 1, gPlayerNameStrings[i]);
                gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {.8f, .8f, .8f, 1};  // Gray for others
            }
            TextMesh_Update(nameStr, 0, gPlayerListText[i]);
        }
        else
        {
            char waitStr[32];
            SDL_snprintf(waitStr, sizeof(waitStr), "%d. WAITING...", i + 1);
            TextMesh_Update(waitStr, 0, gPlayerListText[i]);
            gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {.4f, .4f, .4f, 1};
        }
    }
}


/********************* UPDATE HOST DISPLAY **********************/

static void UpdateHostDisplay(void)
{
    const char* roomCode = GetNetworkRoomCode();
    NetConnectionState state = GetNetworkState();

    // Update status based on state
    switch (state)
    {
        case NET_STATE_CONNECTING_SIGNALING:
            TextMesh_Update("CONNECTING TO SERVER...", 0, gStatusText);
            TextMesh_Update("", 0, gRoomCodeText);
            break;

        case NET_STATE_WAITING_ROOM:
            TextMesh_Update("CREATING ROOM...", 0, gStatusText);
            TextMesh_Update("", 0, gRoomCodeText);
            break;

        case NET_STATE_IN_LOBBY:
        case NET_STATE_CONNECTING_P2P:
        case NET_STATE_CONNECTED:
            TextMesh_Update("SHARE THIS CODE:", 0, gStatusText);
            if (roomCode)
            {
                TextMesh_Update(roomCode, 0, gRoomCodeText);
            }
            break;

        case NET_STATE_ERROR:
            TextMesh_Update(GetNetworkStatusMessage(), 0, gStatusText);
            gStatusText->ColorFilter = (OGLColorRGBA) {1, .3f, .3f, 1};
            break;

        default:
            TextMesh_Update(GetNetworkStatusMessage(), 0, gStatusText);
            break;
    }

    // Update player count and P2P connection status
    int numPlayers = HostGetGatheredPlayerCount();
    int numP2PConnected = HostGetP2PConnectedCount();
    char countStr[64];
    if (numPlayers > 1 && numP2PConnected < numPlayers - 1)
    {
        SDL_snprintf(countStr, sizeof(countStr), "PLAYERS: %d (P2P: %d/%d)",
                     numPlayers, numP2PConnected, numPlayers - 1);
    }
    else
    {
        SDL_snprintf(countStr, sizeof(countStr), "PLAYERS: %d / %d", numPlayers, NET_MAX_PLAYERS);
    }
    TextMesh_Update(countStr, 0, gPlayerCountText);

    // Update player list with connection status
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        if (i < numPlayers)
        {
            char nameStr[48];
            if (i == 0)
            {
                // Host
                SDL_snprintf(nameStr, sizeof(nameStr), "%d. %s (HOST)",
                            i + 1, gPlayerNameStrings[i]);
                gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {1, 1, .5f, 1};  // Yellow for host
            }
            else
            {
                // Client - show connection status
                bool isConnected = (i <= numP2PConnected);  // P2P connections are 1-indexed
                const char* status = isConnected ? "READY" : "connecting...";
                SDL_snprintf(nameStr, sizeof(nameStr), "%d. %s - %s",
                            i + 1, gPlayerNameStrings[i], status);

                if (isConnected)
                    gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};  // Green for ready
                else
                    gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {1, .8f, .2f, 1};  // Orange for connecting
            }
            TextMesh_Update(nameStr, 0, gPlayerListText[i]);
        }
        else
        {
            char waitStr[32];
            SDL_snprintf(waitStr, sizeof(waitStr), "%d. WAITING...", i + 1);
            TextMesh_Update(waitStr, 0, gPlayerListText[i]);
            gPlayerListText[i]->ColorFilter = (OGLColorRGBA) {.4f, .4f, .4f, 1};
        }
    }

    // Update start prompt visibility - need at least 2 players (TCP relay, no P2P needed)
    if (gStartPrompt)
    {
        bool canStart = (numPlayers >= 2);
        if (canStart)
        {
            gStartPrompt->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};
            MakeTwitch(gStartPrompt, kTwitchPreset_PressKeyPrompt);
        }
        else
        {
            gStartPrompt->ColorFilter = (OGLColorRGBA) {.3f, .3f, .3f, 1};
        }
    }
}


/********************* UPDATE JOIN DISPLAY **********************/

static void UpdateJoinDisplay(void)
{
    NetConnectionState state = GetNetworkState();

    // Update room code input display
    char inputDisplay[16];
    if (gRoomCodeCursor < ROOM_CODE_LENGTH)
    {
        // Show input with cursor
        SDL_snprintf(inputDisplay, sizeof(inputDisplay), "%s_", gRoomCodeInput);
        // Pad with underscores for remaining characters
        for (int i = gRoomCodeCursor + 1; i < ROOM_CODE_LENGTH; i++)
        {
            strcat(inputDisplay, "_");
        }
    }
    else
    {
        SDL_snprintf(inputDisplay, sizeof(inputDisplay), "%s", gRoomCodeInput);
    }

    // Only show input when we're still entering the code
    if (state == NET_STATE_DISCONNECTED)
    {
        TextMesh_Update("ENTER ROOM CODE:", 0, gStatusText);
        gStatusText->ColorFilter = (OGLColorRGBA) {1, 1, 1, 1};
        TextMesh_Update(inputDisplay, 0, gRoomCodeInputText);
        TextMesh_Update("", 0, gPlayerCountText);

        // Hide player list
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
        {
            TextMesh_Update("", 0, gPlayerListText[i]);
        }
    }
    else
    {
        // Connecting or connected state
        switch (state)
        {
            case NET_STATE_CONNECTING_SIGNALING:
                TextMesh_Update("CONNECTING TO SERVER...", 0, gStatusText);
                gStatusText->ColorFilter = (OGLColorRGBA) {1, .8f, .2f, 1};  // Yellow
                TextMesh_Update("", 0, gPlayerCountText);
                // Hide player list during connection
                for (int i = 0; i < NET_MAX_PLAYERS; i++)
                {
                    TextMesh_Update("", 0, gPlayerListText[i]);
                }
                break;

            case NET_STATE_WAITING_ROOM:
                TextMesh_Update("JOINING ROOM...", 0, gStatusText);
                gStatusText->ColorFilter = (OGLColorRGBA) {1, .8f, .2f, 1};  // Yellow
                TextMesh_Update("", 0, gPlayerCountText);
                // Hide player list during connection
                for (int i = 0; i < NET_MAX_PLAYERS; i++)
                {
                    TextMesh_Update("", 0, gPlayerListText[i]);
                }
                break;

            case NET_STATE_IN_LOBBY:
            case NET_STATE_CONNECTING_P2P:
            case NET_STATE_CONNECTED:
            {
                // Show appropriate status message
                if (state == NET_STATE_IN_LOBBY)
                {
                    TextMesh_Update("IN LOBBY", 0, gStatusText);
                    gStatusText->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};  // Green
                }
                else if (state == NET_STATE_CONNECTING_P2P)
                {
                    TextMesh_Update("CONNECTING...", 0, gStatusText);
                    gStatusText->ColorFilter = (OGLColorRGBA) {1, .8f, .2f, 1};  // Yellow
                }
                else
                {
                    TextMesh_Update("CONNECTED - READY!", 0, gStatusText);
                    gStatusText->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};  // Green
                }

                // Show player count
                int numPlayers = HostGetGatheredPlayerCount();  // Same function works for client
                char countStr[64];
                SDL_snprintf(countStr, sizeof(countStr), "PLAYERS: %d / %d - WAITING FOR HOST...", numPlayers, NET_MAX_PLAYERS);
                TextMesh_Update(countStr, 0, gPlayerCountText);
                gPlayerCountText->ColorFilter = (OGLColorRGBA) {1, 1, 1, 1};

                // Use shared player list display (isHost = false for client)
                UpdatePlayerList(false);
                break;
            }

            case NET_STATE_ERROR:
                TextMesh_Update(GetNetworkStatusMessage(), 0, gStatusText);
                gStatusText->ColorFilter = (OGLColorRGBA) {1, .3f, .3f, 1};  // Red
                TextMesh_Update("Press ESC to go back", 0, gPlayerCountText);
                // Hide player list on error
                for (int i = 0; i < NET_MAX_PLAYERS; i++)
                {
                    TextMesh_Update("", 0, gPlayerListText[i]);
                }
                // Allow re-entry of room code
                gRoomCodeInput[0] = '\0';
                gRoomCodeCursor = 0;
                break;

            default:
                TextMesh_Update(GetNetworkStatusMessage(), 0, gStatusText);
                break;
        }

        // Show the entered room code
        TextMesh_Update(gRoomCodeInput, 0, gRoomCodeInputText);
    }
}


/***************** UPDATE NAME INPUT DISPLAY *******************/

static void UpdateNameInputDisplay(void)
{
    // Show name input prompt and current input
    char inputDisplay[PLAYER_NAME_MAX_LENGTH + 4];
    SDL_snprintf(inputDisplay, sizeof(inputDisplay), "%s_", gPlayerNameInput);
    TextMesh_Update(inputDisplay, 0, gNameInputText);

    // Show prompt text
    TextMesh_Update("ENTER YOUR NAME:", 0, gNamePromptText);
    gNamePromptText->StatusBits &= ~STATUS_BIT_HIDDEN;
    gNameInputText->StatusBits &= ~STATUS_BIT_HIDDEN;

    // Update status text with hint
    TextMesh_Update("[ENTER] to confirm", 0, gStatusText);
    gStatusText->ColorFilter = (OGLColorRGBA) {.6f, .6f, .6f, 1};

    // Hide other UI elements during name input
    TextMesh_Update("", 0, gRoomCodeText);
    TextMesh_Update("", 0, gPlayerCountText);
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
    {
        TextMesh_Update("", 0, gPlayerListText[i]);
    }
    if (gStartPrompt)
        gStartPrompt->StatusBits |= STATUS_BIT_HIDDEN;
    if (gRoomCodeInputText)
        gRoomCodeInputText->StatusBits |= STATUS_BIT_HIDDEN;
}


/***************** DO NAME INPUT CONTROLS *******************/
//
// Handle player name text input
// Returns: 0 = continue, 1 = name confirmed, -1 = cancelled
//

static int DoNameInputControls(void)
{
    UpdateNameInputDisplay();

    // Backspace to delete (check FIRST before back/cancel)
    if (GetNewKeyState(SDL_SCANCODE_BACKSPACE) && gPlayerNameCursor > 0)
    {
        gPlayerNameInput[--gPlayerNameCursor] = '\0';
        PlayEffect(EFFECT_SELECTCLICK);
        return 0;  // Consume backspace, don't process as back
    }

    // Check for back/cancel (only when name is empty, or ESC key specifically)
    if (gPlayerNameCursor == 0 && GetNewNeedStateAnyP(kNeed_UIBack))
    {
        PlayEffect(EFFECT_SELECTCLICK);
        return -1;
    }

    // Also allow ESC to cancel regardless of input
    if (GetNewKeyState(SDL_SCANCODE_ESCAPE))
    {
        PlayEffect(EFFECT_SELECTCLICK);
        return -1;
    }

    // Handle text input (A-Z, 0-9, space)
    if (gPlayerNameCursor < PLAYER_NAME_MAX_LENGTH)
    {
        // Letters A-Z (always uppercase - font only supports uppercase)
        for (int i = 0; i < 26; i++)
        {
            if (GetNewKeyState(SDL_SCANCODE_A + i))
            {
                char c = 'A' + i;  // Always uppercase
                gPlayerNameInput[gPlayerNameCursor++] = c;
                gPlayerNameInput[gPlayerNameCursor] = '\0';
                PlayEffect(EFFECT_SELECTCLICK);
                break;
            }
        }

        // Numbers 0-9
        for (int i = 0; i <= 9; i++)
        {
            SDL_Scancode key = (i == 0) ? SDL_SCANCODE_0 : (SDL_SCANCODE_1 + (i - 1));
            if (GetNewKeyState(key))
            {
                gPlayerNameInput[gPlayerNameCursor++] = '0' + i;
                gPlayerNameInput[gPlayerNameCursor] = '\0';
                PlayEffect(EFFECT_SELECTCLICK);
                break;
            }
        }

        // Space
        if (GetNewKeyState(SDL_SCANCODE_SPACE) && gPlayerNameCursor > 0)
        {
            gPlayerNameInput[gPlayerNameCursor++] = ' ';
            gPlayerNameInput[gPlayerNameCursor] = '\0';
            PlayEffect(EFFECT_SELECTCLICK);
        }
    }

    // Enter to confirm (require at least 1 character)
    if ((GetNewKeyState(SDL_SCANCODE_RETURN) || GetNewKeyState(SDL_SCANCODE_KP_ENTER)) &&
        gPlayerNameCursor > 0)
    {
        // Trim trailing spaces
        while (gPlayerNameCursor > 0 && gPlayerNameInput[gPlayerNameCursor - 1] == ' ')
        {
            gPlayerNameInput[--gPlayerNameCursor] = '\0';
        }

        if (gPlayerNameCursor > 0)
        {
            PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
            SetLocalPlayerName(gPlayerNameInput);
            gNameInputComplete = true;
            return 1;
        }
    }

    return 0;
}


/***************** HIDE NAME INPUT UI *******************/

static void HideNameInputUI(void)
{
    if (gNamePromptText)
        gNamePromptText->StatusBits |= STATUS_BIT_HIDDEN;
    if (gNameInputText)
        gNameInputText->StatusBits |= STATUS_BIT_HIDDEN;
    if (gStartPrompt)
        gStartPrompt->StatusBits &= ~STATUS_BIT_HIDDEN;
    if (gRoomCodeInputText)
        gRoomCodeInputText->StatusBits &= ~STATUS_BIT_HIDDEN;
}


/***************** DO NETWORK HOST CONTROLS *******************/

static int DoNetworkHostControls(void)
{
    // Process network events
    HostUpdateGathering();

    // Update display
    UpdateHostDisplay();

    // Check for back
    if (GetNewNeedStateAnyP(kNeed_UIBack))
    {
        PlayEffect(EFFECT_SELECTCLICK);
        return -1;  // Cancelled
    }

    // Check for start game (need at least 2 players)
    if (HostGetGatheredPlayerCount() >= 2)
    {
        if (GetNewNeedStateAnyP(kNeed_UIConfirm) ||
            GetNewKeyState(SDL_SCANCODE_RETURN) ||
            GetNewKeyState(SDL_SCANCODE_KP_ENTER))
        {
            PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
            return 1;  // Start game
        }
    }

    // Debug: allow starting with 1 player using cheat key
    if (IsCheatKeyComboDown())
    {
        PlayEffect(EFFECT_ROMANCANDLE_LAUNCH);
        return 1;
    }

    return 0;  // Continue gathering
}


/***************** DO NETWORK JOIN CONTROLS *******************/

static int DoNetworkJoinControls(void)
{
    NetConnectionState state = GetNetworkState();

    // Update display
    UpdateJoinDisplay();

    // If we've received the game config, we're done
    if (state == NET_STATE_CONNECTED)
    {
        // Wait a moment then check for config
        return 0;
    }

    // If in error state, allow retry
    if (state == NET_STATE_ERROR)
    {
        // Reset to allow new input
        Net_CleanupSession();
    }

    // Room code input handling (only when disconnected)
    if (state == NET_STATE_DISCONNECTED)
    {
        // Handle letter input for room code (A-Z, excluding confusing letters)
        if (gRoomCodeCursor < ROOM_CODE_LENGTH)
        {
            // Scan A-Z keys
            for (int i = 0; i < 26; i++)
            {
                if (GetNewKeyState(SDL_SCANCODE_A + i))
                {
                    char letter = 'A' + i;
                    // Skip confusing letters (I, O) - server doesn't use them
                    if (letter != 'I' && letter != 'O')
                    {
                        gRoomCodeInput[gRoomCodeCursor++] = letter;
                        gRoomCodeInput[gRoomCodeCursor] = '\0';
                        PlayEffect(EFFECT_SELECTCLICK);
                    }
                    else
                    {
                        PlayEffect(EFFECT_BADSELECT);
                    }
                    break;
                }
            }

            // Also allow numbers 2-9 (server uses these)
            for (int i = 2; i <= 9; i++)
            {
                SDL_Scancode key = (i == 0) ? SDL_SCANCODE_0 : (SDL_SCANCODE_1 + (i - 1));
                if (GetNewKeyState(key))
                {
                    gRoomCodeInput[gRoomCodeCursor++] = '0' + i;
                    gRoomCodeInput[gRoomCodeCursor] = '\0';
                    PlayEffect(EFFECT_SELECTCLICK);
                    break;
                }
            }
        }

        // Backspace to delete
        if (GetNewKeyState(SDL_SCANCODE_BACKSPACE) && gRoomCodeCursor > 0)
        {
            gRoomCodeInput[--gRoomCodeCursor] = '\0';
            PlayEffect(EFFECT_SELECTCLICK);
            return 0;  // Consume the backspace, don't process as back
        }

        // Enter to join (when code is complete)
        if ((GetNewKeyState(SDL_SCANCODE_RETURN) || GetNewKeyState(SDL_SCANCODE_KP_ENTER)) &&
            gRoomCodeCursor == ROOM_CODE_LENGTH)
        {
            PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
            return 2;  // Join with room code
        }

        // Only allow back when input is empty (ESC or backspace on empty input)
        if (GetNewNeedStateAnyP(kNeed_UIBack) && gRoomCodeCursor == 0)
        {
            PlayEffect(EFFECT_SELECTCLICK);
            return -1;  // Cancelled
        }

        return 0;  // Continue input
    }

    // Check for back (when not in input mode)
    if (GetNewNeedStateAnyP(kNeed_UIBack))
    {
        PlayEffect(EFFECT_SELECTCLICK);
        return -1;  // Cancelled
    }

    return 0;  // Continue
}


/********************** DO NETWORK HOST GATHER SCREEN **************************/
//
// Return true if user aborts.
//

Boolean DoNetworkHostGatherScreen(void)
{
    SetupNetworkGatherScreen(true);
    MakeFadeEvent(true);

                /**************************/
                /* NAME INPUT PHASE       */
                /**************************/

    printf("DoNetworkHostGatherScreen: Starting name input phase\n");
    printf("  gPlayerNameInput = '%s', cursor = %d\n", gPlayerNameInput, gPlayerNameCursor);
    fflush(stdout);

    CalcFramesPerSecond();
    ReadKeyboard();

    int outcome = 0;

    // First: get player name
    while (!gNameInputComplete && outcome == 0)
    {
        outcome = DoNameInputControls();

        CalcFramesPerSecond();
        ReadKeyboard();
        MoveObjects();
        OGL_DrawScene(DrawObjects);
    }

    if (outcome < 0)
    {
        // User cancelled during name input
        goto cleanup;
    }

    // Hide name input UI and show lobby UI
    HideNameInputUI();
    outcome = 0;

    // Setup network hosting with the entered name
    if (SetupNetworkHosting())
    {
        outcome = -1;  // Failed to setup
        goto cleanup;
    }

                /*************/
                /* MAIN LOOP */
                /*************/

    while (outcome == 0)
    {
        outcome = DoNetworkHostControls();

            /**************/
            /* DRAW STUFF */
            /**************/

        CalcFramesPerSecond();
        ReadKeyboard();
        MoveObjects();
        OGL_DrawScene(DrawObjects);
    }

            /***********/
            /* CLEANUP */
            /***********/

cleanup:
    OGL_FadeOutScene(DrawObjects, MoveObjects);

    DisposeNotifications();
    DeleteAllObjects();
    FreeAllSkeletonFiles(-1);
    DisposeAllBG3DContainers();
    OGL_DisposeGameView();

    if (outcome > 0)
    {
        // Send config to all clients and start the game
        HostSendGameConfig();
        return false;  // Success, start game
    }
    else
    {
        // User cancelled
        EndNetworkGame();
        return true;
    }
}


/********************** DO NETWORK JOIN SCREEN **************************/
//
// Return true if user aborts.
//

Boolean DoNetworkJoinScreen(void)
{
    printf("DoNetworkJoinScreen: Starting join screen\n");
    fflush(stdout);

    SetupNetworkGatherScreen(false);
    MakeFadeEvent(true);

                /**************************/
                /* NAME INPUT PHASE       */
                /**************************/

    CalcFramesPerSecond();
    ReadKeyboard();

    int outcome = 0;

    // First: get player name
    while (!gNameInputComplete && outcome == 0)
    {
        outcome = DoNameInputControls();

        CalcFramesPerSecond();
        ReadKeyboard();
        MoveObjects();
        OGL_DrawScene(DrawObjects);
    }

    if (outcome < 0)
    {
        // User cancelled during name input
        goto cleanup;
    }

    // Hide name input UI and show join UI
    HideNameInputUI();
    outcome = 0;

                /*************/
                /* MAIN LOOP */
                /*************/

    bool connectionInitiated = false;

    while (outcome == 0)
    {
        outcome = DoNetworkJoinControls();

        // Handle room code entry completion
        if (outcome == 2 && !connectionInitiated)
        {
            printf("DoNetworkJoinScreen: Joining room %s\n", gRoomCodeInput);
            fflush(stdout);

            if (SetupNetworkJoinWithRoomCode(gRoomCodeInput))
            {
                // Failed to initiate connection
                printf("DoNetworkJoinScreen: Failed to join\n");
                outcome = 0;  // Continue, error will be shown
            }
            else
            {
                connectionInitiated = true;
                outcome = 0;  // Continue waiting for connection
            }
        }

        // Check if we're connected and have config
        if (connectionInitiated)
        {
            NetConnectionState state = GetNetworkState();

            if (state == NET_STATE_ERROR)
            {
                printf("DoNetworkJoinScreen: Connection error, allowing retry\n");
                fflush(stdout);
                connectionInitiated = false;
                // Allow retry
            }
            else if (state == NET_STATE_DISCONNECTED)
            {
                // P2P connection timed out or was lost - treat as error, allow retry
                printf("DoNetworkJoinScreen: Disconnected (P2P timeout?), allowing retry\n");
                fflush(stdout);
                connectionInitiated = false;
                // Reset room code input to allow re-entry
                gRoomCodeInput[0] = '\0';
                gRoomCodeCursor = 0;
            }
            else if (state == NET_STATE_CONNECTED || state == NET_STATE_IN_LOBBY)
            {
                // Try to get config
                if (ClientWaitForConfig())
                {
                    printf("DoNetworkJoinScreen: Got config from host!\n");
                    fflush(stdout);
                    outcome = 1;  // Success
                }
            }

            // Process network events
            Net_ProcessEvents(0);
        }

            /**************/
            /* DRAW STUFF */
            /**************/

        CalcFramesPerSecond();
        ReadKeyboard();
        MoveObjects();
        OGL_DrawScene(DrawObjects);
    }

            /***********/
            /* CLEANUP */
            /***********/

cleanup:
    OGL_FadeOutScene(DrawObjects, MoveObjects);

    DisposeNotifications();
    DeleteAllObjects();
    FreeAllSkeletonFiles(-1);
    DisposeAllBG3DContainers();
    OGL_DisposeGameView();

    printf("DoNetworkJoinScreen: outcome=%d\n", outcome);
    fflush(stdout);

    if (outcome > 0)
    {
        printf("DoNetworkJoinScreen: Success! gameMode=%d, trackNum=%d, playerNum=%d\n",
               gGameMode, gTrackNum, gMyNetworkPlayerNum);
        fflush(stdout);
        return false;  // Success, got config
    }
    else
    {
        // User cancelled
        printf("DoNetworkJoinScreen: Cancelled\n");
        fflush(stdout);
        EndNetworkGame();
        return true;
    }
}
