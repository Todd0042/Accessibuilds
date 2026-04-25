#pragma once
#include <stdint.h>

/*
 * ArcDPS CBTS (Combat Log To Script) event structures.
 * These match the arcdps API as of recent versions.
 */

/* combat result (direct) */
enum cbtresult : uint8_t {
    CBTR_NORMAL    = 0,
    CBTR_CRIT      = 1,
    CBTR_GLANCE    = 2,
    CBTR_BLOCK     = 3,
    CBTR_EVADE     = 4,
    CBTR_INTERRUPT = 5,
    CBTR_ABSORB    = 6,
    CBTR_BLIND     = 7,
    CBTR_KILLINGBLOW = 8,
    CBTR_DOWNED    = 9,
    CBTR_BREAKBAR  = 10,
    CBTR_ACTIVATION= 11,
    CBTR_CROWDCONTROL = 12,
};

/* activation type */
enum cbtactivation : uint8_t {
    ACTV_NONE      = 0,
    ACTV_NORMAL    = 1, /* skill cast started */
    ACTV_QUICKNESS = 2, /* skill cast with quickness */
    ACTV_CANCEL_FIRE = 3, /* skill cancelled but still fires */
    ACTV_CANCEL_CANCEL = 4, /* skill cancelled and does not fire */
    ACTV_RESET     = 5,
};

/* buff remove type */
enum cbtbuffremove : uint8_t {
    CBTB_NONE   = 0,
    CBTB_ALL    = 1,
    CBTB_SINGLE = 2,
    CBTB_MANUAL = 3,
};

/* state change type */
enum cbtstatechange : uint8_t {
    CBTS_NONE              = 0,
    CBTS_ENTERCOMBAT       = 1,
    CBTS_EXITCOMBAT        = 2,
    CBTS_CHANGEUP          = 3,
    CBTS_CHANGEDEAD        = 4,
    CBTS_CHANGDOWN         = 5,
    CBTS_SPAWN             = 6,
    CBTS_DESPAWN           = 7,
    CBTS_HEALTHUPDATE      = 8,
    CBTS_LOGSTART          = 9,
    CBTS_LOGEND            = 10,
    CBTS_WEAPSWAP          = 11,
    CBTS_MAXHEALTHUPDATE   = 12,
    CBTS_POINTOFVIEW       = 13,
    CBTS_LANGUAGE          = 14,
    CBTS_GWBUILD           = 15,
    CBTS_SHARDID           = 16,
    CBTS_REWARD            = 17,
    CBTS_BUFFINITIAL       = 18,
    CBTS_POSITION          = 19,
    CBTS_VELOCITY          = 20,
    CBTS_FACING            = 21,
    CBTS_TEAMCHANGE        = 22,
    CBTS_ATTACKTARGET      = 23,
    CBTS_TARGETABLE        = 24,
    CBTS_MAPID             = 25,
    CBTS_REPLINFO          = 26,
    CBTS_STACKACTIVE       = 27,
    CBTS_STACKRESET        = 28,
    CBTS_GUILD             = 29,
    CBTS_BUFFINFO          = 30,
    CBTS_BUFFFORMULA       = 31,
    CBTS_SKILLINFO         = 32,
    CBTS_SKILLTIMING       = 33,
    CBTS_BREAKBARSTATE     = 34,
    CBTS_BREAKBARPERCENT   = 35,
    CBTS_INTEGRITY         = 36,
    CBTS_MARKER            = 37,
    CBTS_BARRIERINFO       = 38,
    CBTS_STATRESET         = 39,
    CBTS_EXTENSION         = 40,
    CBTS_APIDELAYED        = 41,
    CBTS_INSTANCESTART     = 42,
    CBTS_TICKRATE          = 43,
    CBTS_LAST90BEFOREDOWN  = 44,
    CBTS_EFFECT            = 45,
    CBTS_IDTONONLOCALAGENT = 46,
    CBTS_DISABLELOG        = 47,
};

/* iff (in-fight friend/foe) */
enum iff : uint8_t {
    IFF_FRIEND  = 0,
    IFF_FOE     = 1,
    IFF_UNKNOWN = 2,
};

/* main combat event struct */
struct cbtevent {
    uint64_t time;               /* ms since epoch */
    uint64_t src_agent;
    uint64_t dst_agent;
    int32_t  value;
    int32_t  buff_dmg;
    uint32_t overstack_value;
    uint32_t skillid;
    uint16_t src_instid;
    uint16_t dst_instid;
    uint16_t src_master_instid;
    uint16_t dst_master_instid;
    uint8_t  iff;
    uint8_t  buff;
    uint8_t  result;
    uint8_t  is_activation;      /* cbtactivation */
    uint8_t  is_buffremove;      /* cbtbuffremove */
    uint8_t  is_ninety;
    uint8_t  is_fifty;
    uint8_t  is_moving;
    uint8_t  is_statechange;     /* cbtstatechange */
    uint8_t  is_flanking;
    uint8_t  is_shields;
    uint8_t  is_offcycle;
    uint8_t  pad61;
    uint8_t  pad62;
    uint8_t  pad63;
    uint8_t  pad64;
};

/* agent struct (src/dst descriptions) */
struct ag {
    const char* name;       /* agent name (may be null) */
    uintptr_t   id;         /* unique agent id */
    uint32_t    prof;       /* profession (as in MumbleLink) */
    uint32_t    elite;      /* elite spec */
    uint32_t    self;       /* 1 if this is the local player */
    uint16_t    team;
};

/*
 * ArcDPS Nexus event names (requires ArcDPS Bridge / Arc-to-Nexus bridge addon).
 * If the bridge is not installed these events will not fire and rotation
 * tracking will be disabled (graceful degradation).
 */
#define EV_ARCDPS_COMBATEVENT_LOCAL_RAW  "EV_ARCDPS_COMBATEVENT_LOCAL_RAW"
#define EV_ARCDPS_COMBATEVENT_SQUAD_RAW  "EV_ARCDPS_COMBATEVENT_SQUAD_RAW"

/* Payload passed with the above events */
struct ArcDPSCombatEventArgs {
    cbtevent* ev;   /* may be null (state change only) */
    ag*       src;
    ag*       dst;
    const char* skill_name;
    uint64_t    id;
    uint64_t    revision;
};
