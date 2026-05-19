/****************************/
/* NETWORK GATHER.C         */
/* LAN multiplayer screens  */
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

#define MAX_VISIBLE_GAMES   6


/*********************/
/*    VARIABLES      */
/*********************/

static ObjNode* gStatusText = NULL;
static ObjNode* gPlayerCountText = NULL;
static ObjNode* gGameListText[MAX_VISIBLE_GAMES];
static ObjNode* gBackPrompt = NULL;
static int      gGameListSelection = 0;


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
    ObjNode* titleText = TextMesh_New(title, 0, &def);
    titleText->ColorFilter = (OGLColorRGBA) {1, .8f, .2f, 1};

    // Status text
    def.coord.y = -80;
    def.scale = 0.35f;
    gStatusText = TextMesh_NewEmpty(128, &def);

    // Player count / game list area
    def.coord.y = 0;
    def.scale = 0.3f;
    gPlayerCountText = TextMesh_NewEmpty(64, &def);

    // Initialize game list text objects (for join screen)
    for (int i = 0; i < MAX_VISIBLE_GAMES; i++)
    {
        def.coord.y = 20 + i * 35;
        gGameListText[i] = TextMesh_NewEmpty(64, &def);
        gGameListText[i]->ColorFilter = (OGLColorRGBA) {.7f, .7f, .7f, 1};
    }

    // Back prompt
    def.coord.y = 220;
    def.scale = 0.27f;
    gBackPrompt = TextMesh_New(Localize(STR_PRESS_ESC_TO_GO_BACK), 0, &def);
    gBackPrompt->ColorFilter = (OGLColorRGBA) {.5, .5, .5, 1};
    MakeTwitch(gBackPrompt, kTwitchPreset_PressKeyPrompt);
}


/********************* UPDATE HOST STATUS **********************/

static void UpdateHostStatus(void)
{
    int numPlayers = HostGetGatheredPlayerCount();

    char statusMsg[128];
    SDL_snprintf(statusMsg, sizeof(statusMsg), "%s (%d/%d)",
                 Localize(STR_WAITING_FOR_PLAYERS),
                 numPlayers,
                 MAX_PLAYERS);
    TextMesh_Update(statusMsg, 0, gStatusText);

    // Show player list
    char playerMsg[64];
    SDL_snprintf(playerMsg, sizeof(playerMsg), "%d %s", numPlayers, Localize(STR_PLAYERS));
    TextMesh_Update(playerMsg, 0, gPlayerCountText);

    // Show player names
    for (int i = 0; i < MAX_VISIBLE_GAMES; i++)
    {
        if (i < numPlayers)
        {
            char name[64];
            SDL_snprintf(name, sizeof(name), "%d. %s", i + 1, gPlayerNameStrings[i]);
            TextMesh_Update(name, 0, gGameListText[i]);
            gGameListText[i]->ColorFilter = (OGLColorRGBA) {1, 1, 1, 1};
        }
        else
        {
            TextMesh_Update("", 0, gGameListText[i]);
        }
    }

    // Update "Start Game" prompt when we have 2+ players
    if (numPlayers >= 2)
    {
        TextMesh_Update(Localize(STR_START_GAME), 0, gBackPrompt);
        gBackPrompt->ColorFilter = (OGLColorRGBA) {.2f, 1, .2f, 1};
    }
}


/********************* UPDATE JOIN STATUS **********************/

static void UpdateJoinStatus(void)
{
    int numGames = UpdateLANGameList();

    if (numGames == 0)
    {
        TextMesh_Update(Localize(STR_SCANNING_FOR_GAMES), 0, gStatusText);
        TextMesh_Update(Localize(STR_NO_GAMES_FOUND), 0, gPlayerCountText);

        for (int i = 0; i < MAX_VISIBLE_GAMES; i++)
        {
            TextMesh_Update("", 0, gGameListText[i]);
        }
    }
    else
    {
        char msg[64];
        SDL_snprintf(msg, sizeof(msg), "%d %s", numGames, numGames == 1 ? "GAME FOUND" : "GAMES FOUND");
        TextMesh_Update(msg, 0, gStatusText);
        TextMesh_Update("", 0, gPlayerCountText);

        // Clamp selection
        if (gGameListSelection >= numGames)
            gGameListSelection = numGames - 1;
        if (gGameListSelection < 0)
            gGameListSelection = 0;

        // Show game list
        for (int i = 0; i < MAX_VISIBLE_GAMES; i++)
        {
            if (i < numGames)
            {
                const LANGameInfo* game = GetLANGameInfo(i);
                if (game)
                {
                    char gameStr[64];
                    char ipStr[32];
                    Net_IPToString(game->hostIP, ipStr, sizeof(ipStr));
                    SDL_snprintf(gameStr, sizeof(gameStr), "%s (%d/%d)",
                                 game->gameName, game->currentPlayers, game->maxPlayers);
                    TextMesh_Update(gameStr, 0, gGameListText[i]);

                    // Highlight selected
                    if (i == gGameListSelection)
                    {
                        gGameListText[i]->ColorFilter = (OGLColorRGBA) {1, 1, .2f, 1};
                    }
                    else
                    {
                        gGameListText[i]->ColorFilter = (OGLColorRGBA) {.7f, .7f, .7f, 1};
                    }
                }
            }
            else
            {
                TextMesh_Update("", 0, gGameListText[i]);
            }
        }
    }
}


/***************** DO NETWORK HOST CONTROLS *******************/

static int DoNetworkHostControls(void)
{
    // Process network events
    HostUpdateGathering();

    // Update display
    UpdateHostStatus();

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
    int numGames = GetNumLANGames();

    // Navigate game list
    if (numGames > 0)
    {
        if (GetNewNeedStateAnyP(kNeed_UIUp) || GetNewNeedStateAnyP(kNeed_UIPrev))
        {
            gGameListSelection--;
            if (gGameListSelection < 0)
                gGameListSelection = numGames - 1;
            PlayEffect(EFFECT_SELECTCLICK);
        }
        else if (GetNewNeedStateAnyP(kNeed_UIDown) || GetNewNeedStateAnyP(kNeed_UINext))
        {
            gGameListSelection++;
            if (gGameListSelection >= numGames)
                gGameListSelection = 0;
            PlayEffect(EFFECT_SELECTCLICK);
        }

        // Select game
        if (GetNewNeedStateAnyP(kNeed_UIConfirm) ||
            GetNewKeyState(SDL_SCANCODE_RETURN) ||
            GetNewKeyState(SDL_SCANCODE_KP_ENTER))
        {
            SelectLANGame(gGameListSelection);
            PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
            return 1;  // Join selected game
        }
    }

    // Check for back
    if (GetNewNeedStateAnyP(kNeed_UIBack))
    {
        PlayEffect(EFFECT_SELECTCLICK);
        return -1;  // Cancelled
    }

    return 0;  // Continue scanning
}


/********************** DO NETWORK HOST GATHER SCREEN **************************/
//
// Return true if user aborts.
//

Boolean DoNetworkHostGatherScreen(void)
{
    // Setup network hosting first
    if (SetupNetworkHosting())
    {
        return true;  // Failed to setup
    }

    SetupNetworkGatherScreen(true);
    MakeFadeEvent(true);

                /*************/
                /* MAIN LOOP */
                /*************/

    CalcFramesPerSecond();
    ReadKeyboard();

    int outcome = 0;

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

    OGL_FadeOutScene(DrawObjects, MoveObjects);

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

    gGameListSelection = 0;

    // Start scanning for LAN games
    StartLANGameScan();

    SetupNetworkGatherScreen(false);
    MakeFadeEvent(true);

                /*************/
                /* MAIN LOOP */
                /*************/

    CalcFramesPerSecond();
    ReadKeyboard();

    int outcome = 0;

    while (outcome == 0)
    {
        // Update game list
        UpdateJoinStatus();

        outcome = DoNetworkJoinControls();

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

    StopLANGameScan();

    OGL_FadeOutScene(DrawObjects, MoveObjects);

    DeleteAllObjects();
    FreeAllSkeletonFiles(-1);
    DisposeAllBG3DContainers();
    OGL_DisposeGameView();

    printf("DoNetworkJoinScreen: outcome=%d\n", outcome);
    fflush(stdout);

    if (outcome > 0)
    {
        // Join selected game
        printf("DoNetworkJoinScreen: Calling SetupNetworkJoin...\n");
        fflush(stdout);

        if (SetupNetworkJoin())
        {
            // Failed to connect
            printf("DoNetworkJoinScreen: SetupNetworkJoin FAILED\n");
            fflush(stdout);
            return true;
        }

        printf("DoNetworkJoinScreen: SetupNetworkJoin succeeded, waiting for config...\n");
        fflush(stdout);

        // Blocking wait for config from host
        uint32_t startTick = SDL_GetTicks();
        while (!ClientWaitForConfig())
        {
            if ((SDL_GetTicks() - startTick) > 30000)  // 30 second timeout
            {
                printf("DoNetworkJoinScreen: Timeout waiting for host config\n");
                fflush(stdout);
                EndNetworkGame();
                return true;
            }

            // Check for cancel
            ReadKeyboard();
            if (GetNewNeedStateAnyP(kNeed_UIBack))
            {
                printf("DoNetworkJoinScreen: User cancelled\n");
                fflush(stdout);
                EndNetworkGame();
                return true;
            }

            SDL_Delay(10);
        }

        printf("DoNetworkJoinScreen: Got config from host! gameMode=%d, trackNum=%d, playerNum=%d\n",
               gGameMode, gTrackNum, gMyNetworkPlayerNum);
        fflush(stdout);
        return false;  // Success, got config
    }
    else
    {
        // User cancelled
        printf("DoNetworkJoinScreen: User cancelled (outcome=%d)\n", outcome);
        fflush(stdout);
        return true;
    }
}
