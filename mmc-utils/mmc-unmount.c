#include <gio/gio.h>

#define EXIT_FAILURE 1

static gboolean verbose = 1;
static gboolean success;
static gchar **path;
static GMainLoop *main_loop;

static GOptionEntry  entries[] = {
	{ "verbose", 'v', 0,
	  G_OPTION_ARG_NONE, &verbose,
	  "Whether to enable verbosity",
          NULL },
	{ G_OPTION_REMAINING, 0, 0,
	  G_OPTION_ARG_FILENAME_ARRAY, &path,
	  "Device/mountdir path",
          NULL },

	{ NULL }
};

static void
log_message (const gchar *str,
             ...)
{
  va_list args;

  if (G_LIKELY (!verbose))
    return;

  va_start (args, str);
  g_logv ("ke-recv", G_LOG_LEVEL_MESSAGE, str, args);
  va_end (args);
}

static gboolean
check_device_path (GMount *mount,
                   GFile  *file)
{
  GVolume *volume;
  gchar *device_path;
  GFile *device_file;
  gboolean equal;

  volume = g_mount_get_volume (mount);

  if (!volume)
    return FALSE;

  device_path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
  device_file = g_file_new_for_path (device_path);

  equal = g_file_equal (file, device_file);

  g_object_unref (device_file);
  g_free (device_path);

  return equal;
}

static GMount *
find_mount (GFile *file)
{
  GVolumeMonitor *monitor;
  GMount *mount = NULL;
  GList *mounts, *m;

  monitor = g_volume_monitor_get ();
  mounts = g_volume_monitor_get_mounts (monitor);

  for (m = mounts; m; m = m->next)
    {
      GFile *root;

      root = g_mount_get_root (G_MOUNT (m->data));

      /* Check mount dir/device path */
      if (g_file_equal (root, file) ||
          check_device_path (G_MOUNT (m->data), file))
        {
          log_message ("Found mount for given path");
          g_object_unref (root);
          mount = g_object_ref (m->data);
          break;
        }

      g_object_unref (root);
    }

  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);

  return mount;
}

static void
do_unmount_cb (GObject      *object,
               GAsyncResult *result,
               gpointer      user_data)
{
  GMount *mount;
  GError *error = NULL;

  mount = G_MOUNT (object);

  if (!g_mount_unmount_finish (mount, result, &error))
    {
      log_message ("Could not perform unmount, %s",
                   (error) ? error->message : "No error given");
      success = FALSE;
    }
  else
    {
      log_message ("Unmount succeeded");
      success = TRUE;
    }

  g_main_loop_quit (main_loop);
}

static void
do_unmount (GMount *mount)
{
  g_mount_unmount (mount,
                   G_MOUNT_UNMOUNT_NONE,
                   NULL,
                   do_unmount_cb,
                   NULL);
}

int
main (int argc, char *argv[])
{
  GOptionContext *context;
  GFile *file;
  GMount *mount;

  g_type_init ();

  context = g_option_context_new ("- Unmount a memcard");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, NULL);

  if (!path || !path[0])
    {
      gchar *help_str;

      g_printerr ("No mount or device path were passed\n\n");

      help_str = g_option_context_get_help (context, TRUE, NULL);
      g_option_context_free (context);
      g_printerr ("%s", help_str);
      g_free (help_str);

      return EXIT_FAILURE;
  }

  file = g_file_new_for_commandline_arg (path[0]);
  log_message ("About to unmount %s", path[0]);

  mount = find_mount (file);
  g_object_unref (file);

  if (!mount)
    {
      log_message ("Could not find mount");
      return EXIT_FAILURE;
    }

  do_unmount (mount);
  g_object_unref (mount);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);
  g_main_loop_unref (main_loop);

  /* At this point success is updated with the operation result */

  if (!success)
    return EXIT_FAILURE;

  return 0;
}
