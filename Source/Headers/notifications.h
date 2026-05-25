/****************************/
/*     NOTIFICATIONS.H      */
/* Rolling notification UI  */
/****************************/

#pragma once

//============================================
// CONSTANTS
//============================================

#define MAX_NOTIFICATION_QUEUE      8
#define MAX_VISIBLE_NOTIFICATIONS   4
#define NOTIFICATION_LIFETIME       4.0f
#define NOTIFICATION_TEXT_LENGTH    48

//============================================
// TYPES
//============================================

typedef enum
{
    kNotificationType_PlayerJoined,
    kNotificationType_PlayerDisconnected,
    kNotificationType_ChatMessage,  // Future use
} NotificationType;

//============================================
// PUBLIC API
//============================================

void InitNotifications(void);
void DisposeNotifications(void);
void Notification_PlayerJoined(const char* playerName);
void Notification_PlayerDisconnected(const char* playerName);
