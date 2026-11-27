/* SPDX-License-Identifier: MIT */

/* The host's one player list for every game, its Users interface. The host
 * writes it when disk mode starts or ends and at a few other host events. */

#include "osos-2.0.5.h"
#include "platform.h"

/* What the host reads back per name when it loads the list. */
_Static_assert(EAPP_USER_NAME_SIZE == 64, "name size");

static int name_fits(const char *name)
{
    if (!name || !name[0])
        return 0;
    size_t length = 0;
    while (name[length] && length < EAPP_USER_NAME_SIZE)
        ++length;
    return length < EAPP_USER_NAME_SIZE;
}

uint32_t eapp_user_count(void)
{
    return GameUser_GetCount();
}

int32_t eapp_user_id(uint32_t index)
{
    if (index >= GameUser_GetCount())
        return -1;
    struct GameUser *user = GameUser_GetAt(index);
    return user ? GameUser_GetId(user) : -1;
}

int eapp_user_name(int32_t id, char *out, size_t capacity)
{
    struct GameUser *user = id >= 0 ? GameUser_FindById((uint32_t)id) : NULL;
    if (!user || !out || capacity == 0)
        return -1;
    const char *name = GameUser_GetName(user);
    return eapp_text_copy(out, capacity, name ? name : "");
}

int32_t eapp_user_add(const char *name)
{
    if (!name_fits(name))
        return -1;
    struct GameUser *user = GameUser_Add(name);
    return user ? GameUser_GetId(user) : -1;
}

int eapp_user_rename(int32_t id, const char *name)
{
    struct GameUser *user = id >= 0 ? GameUser_FindById((uint32_t)id) : NULL;
    if (!user || !name_fits(name))
        return -1;
    GameUser_SetName(user, name);
    return 0;
}

int eapp_user_remove(int32_t id)
{
    struct GameUser *user = id >= 0 ? GameUser_FindById((uint32_t)id) : NULL;
    if (!user)
        return -1;
    GameUser_Remove(user);
    return 0;
}
