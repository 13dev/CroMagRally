/****************************/
/*     NOTIFICATIONS.C      */
/* Rolling notification UI  */
/* For multiplayer events   */
/****************************/

/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "notifications.h"
#include <string.h>

/****************************/
/*    PROTOTYPES            */
/****************************/

static void PostNotification(const char* text, NotificationType type);
static void MoveNotification(ObjNode* theNode);
static ObjNode* MakeNotificationTextNode(const char* text, NotificationType type, int slot);
static void RepositionNotifications(void);

/****************************/
/*    CONSTANTS             */
/****************************/

#define NOTIFICATION_X          20.0f       // Left margin
#define NOTIFICATION_Y_BASE     -60.0f      // From bottom
#define NOTIFICATION_Y_SPACING  28.0f       // Stack upward
#define NOTIFICATION_SCALE      0.28f

// Colors
static const OGLColorRGBA kNotificationColorJoin = {0.3f, 1.0f, 0.3f, 1.0f};       // Green
static const OGLColorRGBA kNotificationColorDisconnect = {1.0f, 0.5f, 0.2f, 1.0f}; // Orange-red

/****************************/
/*    TYPES                 */
/****************************/

typedef struct
{
    char            text[NOTIFICATION_TEXT_LENGTH];
    NotificationType type;
    float           timeRemaining;
    ObjNode*        textNode;
    bool            active;
    bool            fading;
} Notification;

// Special data stored in ObjNode
typedef struct
{
    int             slotIndex;      // Index in notification queue
    NotificationType type;
} NotificationNodeData;
CheckSpecialDataStruct(NotificationNodeData);
#define GetNotificationNodeData(node) GetSpecialData(node, NotificationNodeData)

/****************************/
/*    VARIABLES             */
/****************************/

static Notification gNotificationQueue[MAX_NOTIFICATION_QUEUE];
static int          gNotificationHead = 0;      // Next slot to write
static bool         gNotificationsInitialized = false;


/********************* INIT NOTIFICATIONS ***************************/

void InitNotifications(void)
{
    // Clear the queue
    memset(gNotificationQueue, 0, sizeof(gNotificationQueue));
    gNotificationHead = 0;
    gNotificationsInitialized = true;
}


/********************* DISPOSE NOTIFICATIONS ***************************/

void DisposeNotifications(void)
{
    // Text nodes are deleted by DeleteAllObjects, just clear our tracking
    for (int i = 0; i < MAX_NOTIFICATION_QUEUE; i++)
    {
        gNotificationQueue[i].active = false;
        gNotificationQueue[i].textNode = nil;
    }
    gNotificationsInitialized = false;
}


/********************* NOTIFICATION PLAYER JOINED ***************************/

void Notification_PlayerJoined(const char* playerName)
{
    if (!gNotificationsInitialized)
        return;

    char text[NOTIFICATION_TEXT_LENGTH];
    snprintf(text, sizeof(text), "%s JOINED", playerName);

    // Convert to uppercase
    for (int i = 0; text[i]; i++)
    {
        if (text[i] >= 'a' && text[i] <= 'z')
            text[i] = text[i] - 'a' + 'A';
    }

    PostNotification(text, kNotificationType_PlayerJoined);
}


/********************* NOTIFICATION PLAYER DISCONNECTED ***************************/

void Notification_PlayerDisconnected(const char* playerName)
{
    if (!gNotificationsInitialized)
        return;

    char text[NOTIFICATION_TEXT_LENGTH];
    snprintf(text, sizeof(text), "%s DISCONNECTED", playerName);

    // Convert to uppercase
    for (int i = 0; text[i]; i++)
    {
        if (text[i] >= 'a' && text[i] <= 'z')
            text[i] = text[i] - 'a' + 'A';
    }

    PostNotification(text, kNotificationType_PlayerDisconnected);
}


/********************* POST NOTIFICATION ***************************/
//
// Adds a notification to the queue
//

static void PostNotification(const char* text, NotificationType type)
{
    // Only show notifications during actual gameplay
    if (!gIsInGame)
        return;

    // Find the slot to use (circular buffer)
    int slot = gNotificationHead;
    gNotificationHead = (gNotificationHead + 1) % MAX_NOTIFICATION_QUEUE;

    // If this slot had an active notification, delete its node
    if (gNotificationQueue[slot].active && gNotificationQueue[slot].textNode)
    {
        DeleteObject(gNotificationQueue[slot].textNode);
        gNotificationQueue[slot].textNode = nil;
    }

    // Set up the new notification
    Notification* notif = &gNotificationQueue[slot];
    strncpy(notif->text, text, NOTIFICATION_TEXT_LENGTH - 1);
    notif->text[NOTIFICATION_TEXT_LENGTH - 1] = '\0';
    notif->type = type;
    notif->timeRemaining = NOTIFICATION_LIFETIME;
    notif->active = true;
    notif->fading = false;

    // Create the text node
    notif->textNode = MakeNotificationTextNode(text, type, slot);

    // Trigger appear animation
    if (notif->textNode)
    {
        MakeTwitch(notif->textNode, kTwitchPreset_NotificationAppear);
    }

    // Reposition all visible notifications
    RepositionNotifications();
}


/********************* MAKE NOTIFICATION TEXT NODE ***************************/

static ObjNode* MakeNotificationTextNode(const char* text, NotificationType type, int slot)
{
    // Get overlay pane dimensions for positioning
    float lh = gGameView->panes[GetOverlayPaneNumber()].logicalHeight;

    NewObjectDefinitionType def =
    {
        .coord      = {NOTIFICATION_X, lh + NOTIFICATION_Y_BASE, 0},
        .scale      = NOTIFICATION_SCALE,
        .slot       = SPRITE_SLOT + 50,  // Above most UI
        .moveCall   = MoveNotification,
        .flags      = STATUS_BITS_FOR_2D | STATUS_BIT_OVERLAYPANE | STATUS_BIT_MOVEINPAUSE,
        .projection = kProjectionType2DOrthoFullRect,
    };

    ObjNode* textNode = TextMesh_New(text, kTextMeshAlignLeft, &def);

    if (textNode)
    {
        // Set color based on notification type
        if (type == kNotificationType_PlayerJoined)
            textNode->ColorFilter = kNotificationColorJoin;
        else if (type == kNotificationType_PlayerDisconnected)
            textNode->ColorFilter = kNotificationColorDisconnect;

        // Store slot index in special data
        NotificationNodeData* data = GetNotificationNodeData(textNode);
        data->slotIndex = slot;
        data->type = type;
    }

    return textNode;
}


/********************* MOVE NOTIFICATION ***************************/
//
// Per-frame update for notification text nodes
//

static void MoveNotification(ObjNode* theNode)
{
    NotificationNodeData* data = GetNotificationNodeData(theNode);
    int slot = data->slotIndex;

    if (slot < 0 || slot >= MAX_NOTIFICATION_QUEUE)
    {
        DeleteObject(theNode);
        return;
    }

    Notification* notif = &gNotificationQueue[slot];

    // Decrement timer
    notif->timeRemaining -= gFramesPerSecondFrac;

    // Start fade out when time is low
    if (notif->timeRemaining <= 0.5f && !notif->fading)
    {
        notif->fading = true;
        MakeTwitch(theNode, kTwitchPreset_NotificationFadeOut | kTwitchFlags_KillPuppet);
    }

    // Check if fully expired
    if (notif->timeRemaining <= 0.0f)
    {
        notif->active = false;
        notif->textNode = nil;
        // Node will be deleted by the twitch KillPuppet flag, or we delete it now if no twitch
        if (!theNode->TwitchNode)
        {
            DeleteObject(theNode);
        }
        return;
    }
}


/********************* REPOSITION NOTIFICATIONS ***************************/
//
// Stack active notifications from bottom up
//

static void RepositionNotifications(void)
{
    float lh = gGameView->panes[GetOverlayPaneNumber()].logicalHeight;
    float baseY = lh + NOTIFICATION_Y_BASE;

    int visibleCount = 0;

    // Count backwards from most recent to find visible notifications
    // and position them (most recent at bottom, older ones stack up)
    for (int i = 0; i < MAX_NOTIFICATION_QUEUE && visibleCount < MAX_VISIBLE_NOTIFICATIONS; i++)
    {
        // Calculate slot index going backwards from head
        int slot = (gNotificationHead - 1 - i + MAX_NOTIFICATION_QUEUE) % MAX_NOTIFICATION_QUEUE;
        Notification* notif = &gNotificationQueue[slot];

        if (notif->active && notif->textNode)
        {
            // Position: most recent at bottom, older ones above
            notif->textNode->Coord.y = baseY - (visibleCount * NOTIFICATION_Y_SPACING);
            UpdateObjectTransforms(notif->textNode);
            visibleCount++;
        }
    }
}
