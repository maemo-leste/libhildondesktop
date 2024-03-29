/*
 * This file is part of libhildondesktop
 *
 * Copyright (C) 2006, 2008 Nokia Corporation.
 *
 * Based on hd-desktop.c and hd-plugin-manager.c from hildon-desktop.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <string.h>

#include "hd-config.h"

#include "hd-plugin-configuration.h"

#define HD_PLUGIN_CONFIGURATION_CONFIG_GROUP                    "X-PluginManager"
#define HD_PLUGIN_CONFIGURATION_CONFIG_KEY_DEBUG_PLUGINS        "X-Debug-Plugins"
#define HD_PLUGIN_CONFIGURATION_CONFIG_KEY_LOAD_ALL_PLUGINS     "X-Load-All-Plugins"
#define HD_PLUGIN_CONFIGURATION_CONFIG_KEY_PLUGIN_CONFIGURATION "X-Plugin-Configuration"

enum
{
  PROP_0,
  PROP_CONF_FILE,
  PROP_PLUGIN_CONFIG_KEY_FILE,
};

enum
{
  PLUGIN_MODULE_ADDED,
  PLUGIN_MODULE_REMOVED,
  PLUGIN_MODULE_UPDATED,
  CONFIGURATION_LOADED,
  ITEMS_CONFIGURATION_LOADED,
  LAST_SIGNAL
};

struct _HDPluginConfigurationPrivate 
{
  HDConfigFile  *config_file;

  HDConfigFile  *items_config_file;
  GKeyFile      *items_key_file;

  gchar        **plugin_dirs;
  GFile        **plugin_dir_files;
  GFileMonitor **plugin_dir_monitors;

  GHashTable    *available_plugins;

  gboolean       startup;
};

typedef struct _HDPluginConfigurationPrivate HDPluginConfigurationPrivate;

static guint plugin_configuration_signals [LAST_SIGNAL] = { 0 };

/** 
 * SECTION:hd-plugin-configuration
 * @short_description: Manages plugin modules defined by configuration files
 *
 * A #HDPluginConfiguration manages plugin modules defined in configuration 
 * files and .desktop files. 
 *
 * Usually #HDPluginManager should be used which handles the creation of plugins
 * from the configuration.
 * 
 **/

G_DEFINE_TYPE_WITH_PRIVATE (HDPluginConfiguration, hd_plugin_configuration, G_TYPE_OBJECT);

#define HD_PLUGIN_CONFIGURATION_GET_PRIVATE(configuration) \
  ((HDPluginConfigurationPrivate *)hd_plugin_configuration_get_instance_private(configuration))

static void
hd_plugin_configuration_remove_plugin_module (HDPluginConfiguration *configuration,
                                              const gchar     *desktop_file)
{
}

static void
hd_plugin_configuration_plugin_dir_changed (GFileMonitor      *monitor,
                                            GFile             *monitor_file,
                                            GFile             *info,
                                            GFileMonitorEvent  event_type,
                                            HDPluginConfiguration *configuration)
{
  gchar *path = g_file_get_path (monitor_file);
  HDPluginConfigurationPrivate *priv =
      HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration);

  static const char *event_string[] = {"changed", "changes_done", "deleted",
                                       "created","attribute_changed",
                                       "pre-unmount", "unmounted", "moved"};

  g_debug ("%s. path: %s. Event: %s", __FUNCTION__, path,
           event_type <= G_FILE_MONITOR_EVENT_MOVED ?
             event_string [event_type] : "unknown");

  /* Ignore the temporary dpkg files */
  if (!g_str_has_suffix (path, ".desktop"))
    {
      g_free (path);
      return;
    }

  if (event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_CHANGED)
    {
      if (g_hash_table_lookup (priv->available_plugins, path))
        {
          g_debug ("plugin-updated: %s", path);

          g_signal_emit (configuration,
                         plugin_configuration_signals[PLUGIN_MODULE_UPDATED], 0,
                         path);
        }
      else
        {
          g_debug ("plugin-added: %s", path);

          g_hash_table_insert (priv->available_plugins,
                               g_strdup (path),
                               GUINT_TO_POINTER (1));

          g_signal_emit (configuration,
                         plugin_configuration_signals[PLUGIN_MODULE_ADDED], 0,
                         path);
        }
    }
  else if (event_type == G_FILE_MONITOR_EVENT_DELETED)
    {
      g_debug ("plugin-removed: %s", path);

      g_hash_table_remove (priv->available_plugins, path);

      g_signal_emit (configuration,
                     plugin_configuration_signals[PLUGIN_MODULE_REMOVED], 0,
                     path);
    }

    g_free (path);
}

static void
hd_plugin_configuration_init (HDPluginConfiguration *configuration)
{
  /* Get private structure */
  HDPluginConfigurationPrivate *priv =
      HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration);

  priv->startup = TRUE;

  priv->available_plugins = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, NULL);
}

static void
hd_plugin_configuration_plugin_module_added (HDPluginConfiguration *configuration,
                                             const gchar     *desktop_file)
{
  g_return_if_fail (HD_IS_PLUGIN_CONFIGURATION (configuration));
}

static void
hd_plugin_configuration_plugin_module_removed (HDPluginConfiguration *configuration,
                                               const gchar           *desktop_file)
{
  hd_plugin_configuration_remove_plugin_module (configuration, desktop_file);
}

static void
hd_plugin_configuration_finalize (GObject *object)
{
  HDPluginConfigurationPrivate *priv;

  g_return_if_fail (HD_IS_PLUGIN_CONFIGURATION (object));

  priv = HD_PLUGIN_CONFIGURATION_GET_PRIVATE (HD_PLUGIN_CONFIGURATION (object));

  if (priv->config_file)
    priv->config_file = (g_object_unref (priv->config_file), NULL);

  if (priv->plugin_dirs != NULL)
    {
      guint i;

      for (i = 0; priv->plugin_dirs[i] != NULL; i++)
        {
          g_file_monitor_cancel (priv->plugin_dir_monitors[i]);
          g_object_unref (priv->plugin_dir_monitors[i]);
          g_object_unref (priv->plugin_dir_files[i]);
        }
      
      priv->plugin_dir_monitors = (g_free (priv->plugin_dir_monitors), NULL);
      priv->plugin_dir_files = (g_free (priv->plugin_dir_files), NULL);
      priv->plugin_dirs = (g_strfreev (priv->plugin_dirs), NULL);
    }

  if (priv->available_plugins)
    priv->available_plugins = (g_hash_table_destroy (priv->available_plugins), NULL);

  G_OBJECT_CLASS (hd_plugin_configuration_parent_class)->finalize (object);
}

static void
hd_plugin_configuration_load_configuration (HDPluginConfiguration *configuration)
{
  HDPluginConfigurationPrivate *priv =
      HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration);
  GKeyFile *keyfile;

  /* load new configuration */
  keyfile = hd_config_file_load_file (priv->config_file, FALSE);

  if (!keyfile)
    {
      g_warning ("Error loading configuration file");

      return;
    }

  g_signal_emit (configuration, plugin_configuration_signals[CONFIGURATION_LOADED], 0, keyfile);

  g_key_file_free (keyfile);
}

static void
hd_plugin_configuration_load_plugin_configuration (HDPluginConfiguration *configuration)
{
  HDPluginConfigurationPrivate *priv =
      HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration);

  /* Free old plugin configuration */
  if (priv->items_key_file)
    {
      g_key_file_free (priv->items_key_file);
      priv->items_key_file = NULL;
    }

  /* Only load plugin configuration if avaiable */
  if (priv->items_config_file)
    {
      /* Load plugin configuration */
      priv->items_key_file = hd_config_file_load_file (priv->items_config_file, FALSE);

      if (!priv->items_key_file)
        g_warning ("Error loading plugin configuration file");
    }

  /* Use empty keyfile if not set */
  if (!priv->items_key_file)
    priv->items_key_file = g_key_file_new ();

  g_object_notify (G_OBJECT (configuration), "plugin-config-key-file");

  g_signal_emit (configuration,
                 plugin_configuration_signals[ITEMS_CONFIGURATION_LOADED],
                 0,
                 priv->items_key_file);
}

static void
hd_plugin_configuration_configuration_loaded (HDPluginConfiguration *configuration,
                                              GKeyFile        *keyfile)
{
  HDPluginConfigurationPrivate *priv =
      HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration);
  GError *error = NULL;
  gsize n_plugin_dir;
  gchar *items_config_filename;

  /* free old configuration */
  if (priv->plugin_dirs != NULL)
    {
      guint i;

      for (i = 0; priv->plugin_dirs[i] != NULL; i++)
        {
          g_file_monitor_cancel (priv->plugin_dir_monitors[i]);
          g_object_unref (priv->plugin_dir_monitors[i]);
          g_object_unref (priv->plugin_dir_files[i]);
        }

      priv->plugin_dir_monitors = (g_free (priv->plugin_dir_monitors), NULL);
      priv->plugin_dir_files = (g_free (priv->plugin_dir_files), NULL);
      priv->plugin_dirs = (g_strfreev (priv->plugin_dirs), NULL);
    }
  if (priv->items_config_file)
    priv->items_config_file = (g_object_unref (priv->items_config_file), NULL);

  g_hash_table_remove_all (priv->available_plugins);

  /* Load configuration ([X-PluginConfiguration] group) */
  if (!g_key_file_has_group (keyfile, HD_PLUGIN_CONFIGURATION_CONFIG_GROUP))
    {
      g_warning ("Error configuration file doesn't contain group '%s'",
                 HD_PLUGIN_CONFIGURATION_CONFIG_GROUP);

      return;
    }

  priv->plugin_dirs = g_key_file_get_string_list (keyfile,
                                                  HD_PLUGIN_CONFIGURATION_CONFIG_GROUP,
                                                  HD_DESKTOP_CONFIG_KEY_PLUGIN_DIR,
                                                  &n_plugin_dir,
                                                  &error);

  if (!priv->plugin_dirs)
    {
      g_warning ("Error loading configuration file. No plugin dirs defined: %s",
                 error->message);

      g_error_free (error);

      return;
    }
  else 
    {
      guint i;

      priv->plugin_dir_files = g_new0 (GFile*, n_plugin_dir);
      priv->plugin_dir_monitors = g_new0 (GFileMonitor*, n_plugin_dir);

      for (i = 0; priv->plugin_dirs[i] != NULL; i++)
        {
          GDir *dir;
          GError *error = NULL;
          const gchar *name;

          /* Strip spaces */
          g_strstrip (priv->plugin_dirs[i]);

          /* Add monitor */
          priv->plugin_dir_files[i] = g_file_new_for_path (priv->plugin_dirs[i]);
          priv->plugin_dir_monitors[i] =
            g_file_monitor_directory (priv->plugin_dir_files[i],
                                      G_FILE_MONITOR_NONE,
                                      NULL,NULL);
          g_signal_connect (G_OBJECT (priv->plugin_dir_monitors[i]),
                            "changed",
                            G_CALLBACK (hd_plugin_configuration_plugin_dir_changed),
                            (gpointer)configuration);

          /* Get available .desktop files */
          dir = g_dir_open (priv->plugin_dirs[i], 0, &error);

          if (dir == NULL)
            {
              g_warning ("%s. Couldn't read plugin_paths in dir %s. Error: %s",
                         __FUNCTION__,
                         priv->plugin_dirs[i],
                         error->message);
              g_error_free (error);
              continue;
            }

          for (name = g_dir_read_name (dir); name != NULL; name = g_dir_read_name (dir))
            {
              gchar *filename;

              /* Ignore non .desktop files. */
              if (!g_str_has_suffix (name, ".desktop"))
                continue;

              filename = g_build_filename (priv->plugin_dirs[i], name, NULL);

              g_hash_table_insert (priv->available_plugins,
                                   filename,
                                   GUINT_TO_POINTER (1));
            }

          g_dir_close (dir);
        }
    }

  items_config_filename = g_key_file_get_string (keyfile, 
                                                 HD_PLUGIN_CONFIGURATION_CONFIG_GROUP, 
                                                 HD_PLUGIN_CONFIGURATION_CONFIG_KEY_PLUGIN_CONFIGURATION,
                                                 NULL);
  if (items_config_filename)
    {
      gchar *system_conf_dir, *user_conf_dir;

      g_strstrip (items_config_filename);

      /* Get config file directories */
      g_object_get (G_OBJECT (priv->config_file),
                    "system-conf-dir", &system_conf_dir,
                    "user-conf-dir", &user_conf_dir,
                    NULL);

      priv->items_config_file = hd_config_file_new (system_conf_dir,
                                                    user_conf_dir,
                                                    items_config_filename);
      g_signal_connect_object (priv->items_config_file, "changed",
                               G_CALLBACK (hd_plugin_configuration_load_plugin_configuration),
                               configuration, G_CONNECT_SWAPPED);

      g_free (system_conf_dir);
      g_free (user_conf_dir);
    }
  g_free (items_config_filename);

  hd_plugin_configuration_load_plugin_configuration (configuration);
}

static void
hd_plugin_configuration_items_configuration_loaded (HDPluginConfiguration *configuration,
                                                    GKeyFile        *keyfile)
{
}

static void
hd_plugin_configuration_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  HDPluginConfigurationPrivate *priv =
      HD_PLUGIN_CONFIGURATION_GET_PRIVATE (HD_PLUGIN_CONFIGURATION (object));

  switch (prop_id)
    {
    case PROP_CONF_FILE:
      priv->config_file = g_value_get_object (value);
      g_object_ref_sink (priv->config_file);
      if (priv->config_file != NULL)
        g_signal_connect_object (priv->config_file, "changed",
                                 G_CALLBACK (hd_plugin_configuration_load_configuration),
                                 object, G_CONNECT_SWAPPED);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
hd_plugin_configuration_get_property (GObject      *object,
                                      guint         prop_id,
                                      GValue       *value,
                                      GParamSpec   *pspec)
{
  HDPluginConfigurationPrivate *priv =
      HD_PLUGIN_CONFIGURATION_GET_PRIVATE (HD_PLUGIN_CONFIGURATION (object));

  switch (prop_id)
    {
    case PROP_PLUGIN_CONFIG_KEY_FILE:
      g_value_set_pointer (value, priv->items_key_file);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
hd_plugin_configuration_class_init (HDPluginConfigurationClass *klass)
{
  GObjectClass *g_object_class = (GObjectClass *) klass;

  klass->plugin_module_added = hd_plugin_configuration_plugin_module_added;
  klass->plugin_module_removed = hd_plugin_configuration_plugin_module_removed;
  klass->configuration_loaded = hd_plugin_configuration_configuration_loaded;
  klass->items_configuration_loaded = hd_plugin_configuration_items_configuration_loaded;

  g_object_class->finalize = hd_plugin_configuration_finalize;
  g_object_class->get_property = hd_plugin_configuration_get_property;
  g_object_class->set_property = hd_plugin_configuration_set_property;

  g_object_class_install_property (g_object_class,
                                   PROP_CONF_FILE,
                                   g_param_spec_object ("conf-file",
                                                        "conf-file",
                                                        "Configuration file",
                                                        HD_TYPE_CONFIG_FILE,
                                                        G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (g_object_class,
                                   PROP_PLUGIN_CONFIG_KEY_FILE,
                                   g_param_spec_pointer ("plugin-config-key-file",
                                                         "Plugin Config Key File",
                                                         "Plugin configuration key file",
                                                         G_PARAM_READABLE));

  /**
   *  HDPluginConfiguration::plugin-module-added:
   *  @configuration: a #HDPluginConfiguration.
   *  @desktop_file: filename of the plugin desktop file.
   *
   *  Emitted if a new plugin desktop file is installed.
   **/
  plugin_configuration_signals [PLUGIN_MODULE_ADDED] = g_signal_new ("plugin-module-added",
                                                                     G_TYPE_FROM_CLASS (klass),
                                                                     G_SIGNAL_RUN_FIRST,
                                                                     G_STRUCT_OFFSET (HDPluginConfigurationClass, plugin_module_added),
                                                                     NULL, NULL,
                                                                     g_cclosure_marshal_VOID__STRING,
                                                                     G_TYPE_NONE, 1,
                                                                     G_TYPE_STRING);

  /**
   *  HDPluginConfiguration::plugin-module-removed:
   *  @configuration: a #HDPluginConfiguration.
   *  @desktop_file: filename of the plugin desktop file.
   *
   *  Emitted if a plugin desktop file is removed.
   **/
  plugin_configuration_signals [PLUGIN_MODULE_REMOVED] = g_signal_new ("plugin-module-removed",
                                                                       G_TYPE_FROM_CLASS (klass),
                                                                       G_SIGNAL_RUN_FIRST,
                                                                       G_STRUCT_OFFSET (HDPluginConfigurationClass, plugin_module_removed),
                                                                       NULL, NULL,
                                                                       g_cclosure_marshal_VOID__STRING,
                                                                       G_TYPE_NONE, 1,
                                                                       G_TYPE_STRING);

  /**
   *  HDPluginConfiguration::plugin-module-updated:
   *  @configuration: a #HDPluginConfiguration.
   *  @desktop_file: filename of the plugin desktop file.
   *
   *  Emitted if a plugin desktop file is updated.
   **/
  plugin_configuration_signals [PLUGIN_MODULE_UPDATED] = g_signal_new ("plugin-module-updated",
                                                                       G_TYPE_FROM_CLASS (klass),
                                                                       G_SIGNAL_RUN_FIRST,
                                                                       0, /* No class method associated */
                                                                       NULL, NULL,
                                                                       g_cclosure_marshal_VOID__STRING,
                                                                       G_TYPE_NONE, 1,
                                                                       G_TYPE_STRING);

  /**
   *  HDPluginConfiguration::configuration-loaded:
   *  @configuration: a #HDPluginConfiguration.
   *  @key_file: the plugin configuration configuration #GKeyFile.
   *
   *  Emitted if the plugin configuration configuration file is loaded.
   **/
  plugin_configuration_signals [CONFIGURATION_LOADED] = g_signal_new ("configuration-loaded",
                                                                      G_TYPE_FROM_CLASS (klass),
                                                                      G_SIGNAL_RUN_LAST,
                                                                      G_STRUCT_OFFSET (HDPluginConfigurationClass,
                                                                                       configuration_loaded),
                                                                      NULL, NULL,
                                                                      g_cclosure_marshal_VOID__POINTER,
                                                                      G_TYPE_NONE, 1,
                                                                      G_TYPE_POINTER);

  /**
   *  HDPluginConfiguration::plugin-configuration-loaded:
   *  @configuration: a #HDPluginConfiguration.
   *  @key_file: the plugin configuration #GKeyFile.
   *
   *  Emitted if the plugin configuration file is loaded.
   **/
  plugin_configuration_signals [ITEMS_CONFIGURATION_LOADED] = g_signal_new ("items-configuration-loaded",
                                                                            G_TYPE_FROM_CLASS (klass),
                                                                            G_SIGNAL_RUN_LAST,
                                                                            G_STRUCT_OFFSET (HDPluginConfigurationClass,
                                                                                             items_configuration_loaded),
                                                                            NULL, NULL,
                                                                            g_cclosure_marshal_VOID__POINTER,
                                                                            G_TYPE_NONE, 1,
                                                                            G_TYPE_POINTER);
}

/**
 * hd_plugin_configuration_new:
 * @config_file: a HDConfigFile which specify the configuration file.
 *
 * This function creates a new #HDPluginConfiguration instance.
 *
 * Returns: a new #HDPluginConfiguration instance.
 **/
HDPluginConfiguration *
hd_plugin_configuration_new (HDConfigFile *config_file)
{
  HDPluginConfiguration *configuration = g_object_new (HD_TYPE_PLUGIN_CONFIGURATION,
                                                       "conf-file", config_file,
                                                       NULL);

  return configuration;
}

/**
 * hd_plugin_configuration_run:
 * @configuration: a #HDPluginConfiguration
 *
 * This function should be called after the callback signals
 * are connected to @configuration. It does an initial read of the configuration
 * files, loads the plugins according to the configuration and emits the
 * appropiate callback signals.
 **/
void
hd_plugin_configuration_run (HDPluginConfiguration *configuration)
{
  g_return_if_fail (HD_IS_PLUGIN_CONFIGURATION (configuration));

  hd_plugin_configuration_load_configuration (configuration);
  HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration)->startup = FALSE;
}

GHashTable *
hd_plugin_configuration_get_available_plugins (HDPluginConfiguration *configuration)
{
  g_return_val_if_fail (HD_IS_PLUGIN_CONFIGURATION (configuration), NULL);

  return HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration)->available_plugins;
}

gchar **
hd_plugin_configuration_get_all_plugin_paths (HDPluginConfiguration *configuration)
{
  HDPluginConfigurationPrivate *priv;
  GHashTableIter iter;
  gpointer key, value;
  GPtrArray *plugin_paths;

  g_return_val_if_fail (HD_IS_PLUGIN_CONFIGURATION (configuration), NULL);

  priv = HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration);
  plugin_paths = g_ptr_array_new ();

  /* Iterate over available plugins */
  g_hash_table_iter_init (&iter, priv->available_plugins);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_ptr_array_add (plugin_paths, g_strdup (key));
    }

  /* Should return a NULL terminated array */
  g_ptr_array_add (plugin_paths, NULL);

  return (gchar **) g_ptr_array_free (plugin_paths, FALSE);
}

/**
 * hd_plugin_configuration_get_items_key_file:
 * @configuration: a #HDPluginConfiguration
 *
 * This function can be used in the HDPluginConfiguration::plugin-added and
 * HDPluginConfiguration::plugin-configuration-loaded to get a reference
 * of the plugin configuration key file.
 *
 * Returns: a reference to the plugin configuration key file. It is owned by the configuration and must not be freed.
 **/
GKeyFile *
hd_plugin_configuration_get_items_key_file (HDPluginConfiguration *configuration)
{
  g_return_val_if_fail (HD_IS_PLUGIN_CONFIGURATION (configuration), NULL);

  return HD_PLUGIN_CONFIGURATION_GET_PRIVATE(configuration)->items_key_file;
}

/**
 * hd_plugin_configuration_store_items_key_file:
 * @configuration: a #HDPluginConfiguration
 *
 * Stores an updated plugin configuration key file back to disk.
 *
 * Returns: %TRUE when file was successful stored.
 **/
gboolean
hd_plugin_configuration_store_items_key_file (HDPluginConfiguration *configuration)
{
  HDPluginConfigurationPrivate *priv;

  g_return_val_if_fail (HD_IS_PLUGIN_CONFIGURATION (configuration), FALSE);

  priv = HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration);

  if (priv->items_config_file)
    return hd_config_file_save_file (priv->items_config_file, priv->items_key_file);

  return FALSE;
}


/**
 * hd_plugin_configuration_get_in_startup:
 * @configuration: a #HDPluginConfiguration
 *
 * Returns if the configuration is just reading the configuration files for the
 * first time after startup.
 *
 * Returns: %TRUE when the configuration files are read for startup.
 **/
gboolean
hd_plugin_configuration_get_in_startup (HDPluginConfiguration *configuration)
{
  g_return_val_if_fail (HD_IS_PLUGIN_CONFIGURATION (configuration), FALSE);

  return HD_PLUGIN_CONFIGURATION_GET_PRIVATE (configuration)->startup;
}
