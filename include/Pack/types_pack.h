#ifndef RP_TYPES_H
#define RP_TYPES_H
#include <types.h>

/**
 * @brief Holds year, month, month day
 * @note Only years after (and including) 2000 can be represented.
 */
typedef u16 RPTime16;

/**
 * @brief Holds year, month, month day, week day, hour
 * @note Only years after (and including) 2000 can be represented.
 */
typedef u32 RPTime32;

/**
 * @brief `unsigned int`, the ABI's other 32-bit unsigned type (u32 is `unsigned long`)
 * @details The RPAudio sound actors have overload pairs that take a sound ID as either type.
 * On the PC port u32 is `unsigned int`, so ui32 is another type there to keep the pairs (and
 * their vtable slots); a call with a u32 or `unsigned int` argument picks the u32 overload,
 * which does the same thing.
 */
#if defined(PORT_HOST)
typedef unsigned long ui32;
#else
typedef unsigned int ui32;
#endif

//! Maximum number of connected remotes
#define RP_MAX_CONTROLLERS 4

//! Maximum number of players in Wii Sports
#define RP_SPORTS_MAX_PLAYERS 4
//! Maximum number of players in Wii Play
#define RP_PARTY_MAX_PLAYERS 2
//! Maximum number of players in Wii Fit
#define RP_HEALTH_MAX_PLAYERS 2
//! Maximum number of players in Wii Music
#define RP_MUSIC_MAX_PLAYERS 2

//! Maximum number of players in the current pack
#if defined(PACK_SPORTS)
#define RP_MAX_PLAYERS RP_SPORTS_MAX_PLAYERS
#elif defined(PACK_PARTY)
#define RP_MAX_PLAYERS RP_PARTY_MAX_PLAYERS
#elif defined(PACK_HEALTH)
#define RP_MAX_PLAYERS RP_HEALTH_MAX_PLAYERS
#elif defined(PACK_MUSIC)
#define RP_MAX_PLAYERS RP_MUSIC_MAX_PLAYERS
#endif

/**
 * List for-each macro.
 *
 * @param TYPE Element type
 * @param NAME Element name
 * @param LIST Reference to list
 */
#define RP_NW4R_LIST_FOREACH(TYPE, NAME, LIST)                                 \
    for (TYPE* NAME = static_cast<TYPE*>(nw4r::ut::List_GetFirst(&LIST));      \
         NAME != NULL;                                                         \
         NAME = static_cast<TYPE*>(nw4r::ut::List_GetNext(&LIST, NAME)))

/**
 * @brief Generic BMG message holder
 */
struct bmg_message {
    u32 group;
    u32 index;
};

#define MSG_GROUP(X) (((X) & 0xFFFFFFF0) >> 4)
#define MSG_INDEX(X) ((X) & 0x0000000F)
#define MSG_TUPLE(X) MSG_GROUP(X), MSG_INDEX(X)

#endif
