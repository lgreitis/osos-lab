#include "custom_eq.h"
#include "osos.h"

static uint32_t settings[CFW_EQ_FIELDS];
static int initialized;
static unsigned int save_status;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWEQ"};

struct eq_file {
    uint32_t magic, version;
    uint32_t values[CFW_EQ_FIELDS];
    uint32_t checksum;
};

static int equal(const char *a, const char *b)
{
    if (!a)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static uint32_t checksum(const struct eq_file *file)
{
    const unsigned char *bytes = (const unsigned char *)file;
    uint32_t hash = 2166136261u;
    for (unsigned int i = 0; i < sizeof(*file) - 4; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void defaults(void)
{
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = cfw_eq_fields[i].default_value;
    cfw_eq_publish(settings);
    initialized = 1;
}

void cfw_eq_load_preferences(void)
{
    struct eq_file file;
    void *handle;
    uint32_t count = 0;
    defaults();
    if (osos_file_open(&settings_path, 1, &handle) != 0)
        return;
    int result = osos_file_read(handle, &file, sizeof(file), &count);
    osos_file_close(handle);
    if (result || count != sizeof(file) || file.magic != 0x51455743 ||
        file.version != 1 || file.checksum != checksum(&file))
        return;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        if (file.values[i] >= cfw_eq_fields[i].count)
            return;
    if (!cfw_eq_publish(file.values))
        return;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        settings[i] = file.values[i];
}

static void save_preferences(void)
{
    struct eq_file file;
    void *handle;
    uint32_t count = 0;
    file.magic = 0x51455743;
    file.version = 1;
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        file.values[i] = settings[i];
    file.checksum = checksum(&file);
    save_status = 1;
    if (osos_file_open(&settings_path, 2, &handle) != 0)
        return;
    int result = osos_file_write(handle, &file, sizeof(file), &count);
    osos_file_close(handle);
    if (!result && count == sizeof(file))
        save_status = 0;
}

static int set_label(uint32_t table, unsigned int index, uint32_t source)
{
    uint32_t size;
    unsigned char *data =
        osos_resource_get(osos_resource_bank(), 0x4954454d, table, &size);
    /* Generated ITEM blocks have an eight-byte header and a 0x98-byte body. */
    unsigned int offset = 12 + index * 0xa0;
    if (!data || size < offset + 0x98)
        return 0;
    if (*(uint32_t *)(data + offset + 0x68) == source)
        return 0;
    *(uint32_t *)(data + offset + 0x68) = source;
    return 1;
}

static void refresh_field(unsigned int index)
{
    const struct cfw_eq_field *field = &cfw_eq_fields[index];
    unsigned int selected = settings[index];
    set_label(field->parent_table, field->parent_index, field->summaries[selected]);
    for (unsigned int i = 0; i < field->count; i++)
        set_label(field->selector_table, i,
                  i == selected ? field->marked[i] : field->labels[i]);
    osos_menu_item_changed(field->parent_item);
}

static void refresh_status(void)
{
    set_label(cfw_eq_main_table, 4, cfw_eq_save_labels[save_status]);
    osos_menu_item_changed(cfw_eq_save_item);
}

static int selection(const char *action, unsigned int *field, unsigned int *value)
{
    static const char prefix[] = "CFW_EQ_Set_";
    unsigned int i = 0;
    if (!action)
        return 0;
    while (prefix[i] && action[i] == prefix[i])
        i++;
    if (prefix[i])
        return 0;
    const char *digits = action + i;
    for (unsigned int n = 0; n < 5; n++) {
        if (n == 2) {
            if (digits[n] != '_')
                return 0;
        } else if (digits[n] < '0' || digits[n] > '9') {
            return 0;
        }
    }
    if (digits[5])
        return 0;
    *field = (digits[0] - '0') * 10 + digits[1] - '0';
    *value = (digits[3] - '0') * 10 + digits[4] - '0';
    return *field < CFW_EQ_FIELDS && *value < cfw_eq_fields[*field].count;
}

int cfw_settings_action(void *controller, const char *action, uint32_t argument)
{
    unsigned int field, value;
    int open = equal(action, "CFW_EQ_Open");
    int save = equal(action, "CFW_EQ_Save");
    int chosen = selection(action, &field, &value);
    if (open || save || chosen) {
        if (argument == 0xdeadbeef)
            return 1;
        if (!initialized)
            defaults();
        if (chosen) {
            unsigned int previous = settings[field];
            settings[field] = value;
            if (cfw_eq_publish(settings)) {
                save_preferences();
                refresh_field(field);
                osos_menu_item_changed(cfw_eq_fields[field].items[previous]);
                osos_menu_item_changed(cfw_eq_fields[field].items[value]);
            } else {
                settings[field] = previous;
                save_status = 2;
            }
        }
        if (save)
            save_preferences();
        if (open)
            for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
                refresh_field(i);
        refresh_status();
        return 1;
    }
    return osos_settings_action(controller, action, argument);
}
