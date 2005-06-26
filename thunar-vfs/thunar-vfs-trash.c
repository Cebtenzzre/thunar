/* $Id$ */
/*-
 * Copyright (c) 2005 Benedikt Meurer <benny@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include <exo/exo.h>

#include <thunar-vfs/thunar-vfs-trash.h>

/* we use g_stat() if possible */
#if GLIB_CHECK_VERSION(2,6,0)
#include <glib/gstdio.h>
#endif



static ThunarVfsTrashInfo *thunar_vfs_trash_info_new (const gchar *path,
                                                      const gchar *deletion_date);



struct _ThunarVfsTrashInfo
{
  guint deletion_date_offset;
};



GType
thunar_vfs_trash_info_get_type (void)
{
  static GType type = G_TYPE_INVALID;

  if (G_UNLIKELY (type == G_TYPE_INVALID))
    {
      type = g_boxed_type_register_static ("ThunarVfsTrashInfo",
                                           (GBoxedCopyFunc) thunar_vfs_trash_info_copy,
                                           (GBoxedFreeFunc) thunar_vfs_trash_info_free);
    }

  return type;
}



static ThunarVfsTrashInfo*
thunar_vfs_trash_info_new (const gchar *path,
                           const gchar *deletion_date)
{
  ThunarVfsTrashInfo *info;
  gsize               path_len;
  gsize               deletion_date_len;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (deletion_date != NULL, NULL);

  path_len = strlen (path) + 1;
  deletion_date_len = strlen (deletion_date) + 1;

  /* allocate memory for the info object */
  info = g_malloc (sizeof (*info) + path_len + deletion_date_len);

  /* setup the proper offset */
  info->deletion_date_offset = sizeof (*info) + path_len;

  /* copy the strings */
  memcpy (((gchar *) info) + sizeof (*info), path, path_len);
  memcpy (((gchar *) info) + sizeof (*info) + path_len, deletion_date, deletion_date_len);

  return info;
}



/**
 * thunar_vfs_trash_info_copy:
 * @info : a #ThunarVfsTrashInfo.
 *
 * Creates a new #ThunarVfsTrashInfo as a copy of @info.
 *
 * Return value: the newly allocated #ThunarVfsTrashInfo.
 **/
ThunarVfsTrashInfo*
thunar_vfs_trash_info_copy (const ThunarVfsTrashInfo *info)
{
  g_return_val_if_fail (info != NULL, NULL);
  return g_memdup (info, sizeof (*info));
}



/**
 * thunar_vfs_trash_info_free:
 * @info : a #ThunarVfsTrashInfo.
 *
 * Frees the resources allocated to @info.
 **/
void
thunar_vfs_trash_info_free (ThunarVfsTrashInfo *info)
{
  g_return_if_fail (info != NULL);
  g_free (info);
}



/**
 * thunar_vfs_trash_info_get_path:
 * @info : a #ThunarVfsTrashInfo.
 *
 * Returns the original path stored with @info.
 *
 * Return value: the original path.
 **/
const gchar*
thunar_vfs_trash_info_get_path (const ThunarVfsTrashInfo *info)
{
  g_return_val_if_fail (info != NULL, NULL);
  return ((gchar *) info) + sizeof (*info);
}



/**
 * thunar_vfs_trash_info_get_deletion_date:
 * @info : a #ThunarVfsTrashInfo.
 *
 * Returns the deletion date of @info.
 *
 * Return value: the deletion date.
 **/
const gchar*
thunar_vfs_trash_info_get_deletion_date (const ThunarVfsTrashInfo *info)
{
  g_return_val_if_fail (info != NULL, NULL);
  return ((gchar *) info) + info->deletion_date_offset;
}




/* interval in which trashes will be checked */
#define THUNAR_VFS_TRASH_INTERVAL (5 * 1000)



enum
{
  THUNAR_VFS_TRASH_PROP_0,
  THUNAR_VFS_TRASH_PROP_FILES,
};



static void            thunar_vfs_trash_class_init           (ThunarVfsTrashClass *klass);
static void            thunar_vfs_trash_init                 (ThunarVfsTrash      *trash);
static void            thunar_vfs_trash_finalize             (GObject             *object);
static void            thunar_vfs_trash_get_property         (GObject             *object,
                                                              guint                prop_id,
                                                              GValue              *value,
                                                              GParamSpec          *pspec);
static void            thunar_vfs_trash_update               (ThunarVfsTrash      *trash);
static gboolean        thunar_vfs_trash_update_timer         (gpointer             user_data);
static void            thunar_vfs_trash_update_timer_destroy (gpointer             user_data);
static ThunarVfsTrash *thunar_vfs_trash_new_internal         (gchar               *directory);



struct _ThunarVfsTrashClass
{
  GtkObjectClass __parent__;
};

struct _ThunarVfsTrash
{
  GtkObject __parent__;

  gint     id;
  GList   *files;
  gchar   *directory;
  gchar   *files_directory;

  time_t   update_last_ctime;
  gint     update_timer_id;
};



G_DEFINE_TYPE (ThunarVfsTrash, thunar_vfs_trash, GTK_TYPE_OBJECT);



static void
thunar_vfs_trash_class_init (ThunarVfsTrashClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = thunar_vfs_trash_finalize;
  gobject_class->get_property = thunar_vfs_trash_get_property;

  /**
   * ThunarVfsTrash:files:
   *
   * The list of files stored within this trash. The list contains
   * only the basename of each file relative to the files/ subdirectory.
   **/
  g_object_class_install_property (gobject_class,
                                   THUNAR_VFS_TRASH_PROP_FILES,
                                   g_param_spec_pointer ("files",
                                                         _("Files"),
                                                         _("The list of files stored within this trash"),
                                                         EXO_PARAM_READABLE));
}



static void
thunar_vfs_trash_init (ThunarVfsTrash *trash)
{
  trash->update_last_ctime = (time_t) -1;
  trash->update_timer_id = -1;
}



static void
thunar_vfs_trash_finalize (GObject *object)
{
  ThunarVfsTrash *trash = THUNAR_VFS_TRASH (object);
  GList          *lp;

  /* stop the update timer */
  if (G_LIKELY (trash->update_timer_id >= 0))
    g_source_remove (trash->update_timer_id);

  /* free the list of files */
  for (lp = trash->files; lp != NULL; lp = lp->next)
    g_free ((gchar *) lp->data);
  g_list_free (trash->files);

  /* free the files directory path */
  g_free (trash->files_directory);

  /* free the base directory path */
  g_free (trash->directory);

  G_OBJECT_CLASS (thunar_vfs_trash_parent_class)->finalize (object);
}



static void
thunar_vfs_trash_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  ThunarVfsTrash *trash = THUNAR_VFS_TRASH (object);

  switch (prop_id)
    {
    case THUNAR_VFS_TRASH_PROP_FILES:
      g_value_set_pointer (value, thunar_vfs_trash_get_files (trash));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
thunar_vfs_trash_update (ThunarVfsTrash *trash)
{
  const gchar *name;
  struct stat  sb;
  gboolean     emit = FALSE;
  time_t       ctime = (time_t) -1;
  GList       *new_files = NULL;
  GList       *lp;
  GDir        *dp;

  /* stat the files/ subdirectory */
#if GLIB_CHECK_VERSION(2,6,0)
  if (g_stat (trash->files_directory, &sb) == 0)
    ctime = sb.st_ctime;
#else
  if (stat (trash->files_directory, &sb) == 0)
    ctime = sb.st_ctime;
#endif

  /* update only if the ctimes differ */
  if (G_LIKELY (ctime == trash->update_last_ctime))
    return;
  else
    trash->update_last_ctime = ctime;

  dp = g_dir_open (trash->files_directory, 0, NULL);
  if (G_LIKELY (dp != NULL))
    {
      for (;;)
        {
          /* read the next entry */
          name = g_dir_read_name (dp);
          if (name == NULL)
            break;

          /* ignore '.' and '..' entries */
          if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

          /* check if we already know about that file */
          for (lp = trash->files; lp != NULL; lp = lp->next)
            if (exo_str_is_equal (lp->data, name))
              break;

          if (lp != NULL)
            {
              trash->files = g_list_remove_link (trash->files, lp);
              new_files = g_list_concat (lp, new_files);
            }
          else
            {
              new_files = g_list_prepend (new_files, g_strdup (name));
              emit = TRUE;
            }
        }

      g_dir_close (dp);
    }
  
  /* check if we have leftover files (deleted files) */
  if (trash->files != NULL)
    {
      for (lp = trash->files; lp != NULL; lp = lp->next)
        g_free ((gchar *) lp->data);
      g_list_free (trash->files);
      trash->files = NULL;
      emit = TRUE;
    }

  /* activate the new files list */
  trash->files = new_files;

  /* emit notification (if necessary) */
  if (G_UNLIKELY (emit))
    g_object_notify (G_OBJECT (trash), "files");
}



static gboolean
thunar_vfs_trash_update_timer (gpointer user_data)
{
  GDK_THREADS_ENTER ();
  thunar_vfs_trash_update (THUNAR_VFS_TRASH (user_data));
  GDK_THREADS_LEAVE ();
  return TRUE;
}



static void
thunar_vfs_trash_update_timer_destroy (gpointer user_data)
{
  GDK_THREADS_ENTER ();
  THUNAR_VFS_TRASH (user_data)->update_timer_id = -1;
  GDK_THREADS_LEAVE ();
}



static ThunarVfsTrash*
thunar_vfs_trash_new_internal (gchar *directory)
{
  ThunarVfsTrash *trash;

  g_return_val_if_fail (directory != NULL, NULL);
  g_return_val_if_fail (g_path_is_absolute (directory), NULL);

  /* allocate the trash object (directory string is taken!) */
  trash = g_object_new (THUNAR_VFS_TYPE_TRASH, NULL);
  trash->directory = directory;
  trash->files_directory = g_build_filename (directory, "files", NULL);

  /* force an update to read the current trash contents */
  thunar_vfs_trash_update (trash);

  /* schedule a timer to regularly check the trash contents */
  trash->update_timer_id = g_timeout_add_full (G_PRIORITY_LOW, THUNAR_VFS_TRASH_INTERVAL,
                                               thunar_vfs_trash_update_timer, trash,
                                               thunar_vfs_trash_update_timer_destroy);

  /* drop the floating reference */
  g_object_ref (G_OBJECT (trash));
  gtk_object_sink (GTK_OBJECT (trash));

  return trash;
}



/**
 * thunar_vfs_trash_get_id:
 * @trash : a #ThunarVfsTrash.
 *
 * Returns the unique id of the @trash.
 *
 * Return value: the unique id of @trash.
 **/
gint
thunar_vfs_trash_get_id (ThunarVfsTrash *trash)
{
  g_return_val_if_fail (THUNAR_VFS_IS_TRASH (trash), 0);
  return trash->id;
}



/**
 * thunar_vfs_trash_get_files:
 * @trash : a #ThunarVfsTrash.
 *
 * Returns the list of files stored within @trash. The returned
 * list contains only the basename of each file relative to the
 * @trash<!---->s files/ subdirectory.
 *
 * The returned list and the string contained within the list
 * are owned by @trash and must not be freed by the caller.
 *
 * Return value: the list of files stored within @trash.
 **/
GList*
thunar_vfs_trash_get_files (ThunarVfsTrash *trash)
{
  g_return_val_if_fail (THUNAR_VFS_IS_TRASH (trash), NULL);
  return trash->files;
}



/**
 * thunar_vfs_trash_get_info:
 * @trash : a #ThunarVfsTrash.
 * @file  : the basename of the trashed file.
 *
 * Determines the trash info for the given @file in @trash. The trash
 * info currently contains the original path and deletion date of the
 * @file.
 *
 * This function may return %NULL if there's no trash info associated
 * with @file, even tho the file is listed as being stored within
 * @trash.
 *
 * The caller is responsible for freeing the returned trash info
 * object using the #thunar_vfs_trash_info_free() function.
 *
 * Return value: the trash info for @file or %NULL.
 **/
ThunarVfsTrashInfo*
thunar_vfs_trash_get_info (ThunarVfsTrash *trash,
                           const gchar    *file)
{
  ThunarVfsTrashInfo *info = NULL;
  const gchar        *path;
  const gchar        *date;
  XfceRc             *rc;
  gchar              *info_path;

  g_return_val_if_fail (THUNAR_VFS_IS_TRASH (trash), NULL);
  g_return_val_if_fail (file != NULL, NULL);

  info_path = g_strconcat (trash->directory, G_DIR_SEPARATOR_S, "info", G_DIR_SEPARATOR_S, file, ".trashinfo", NULL);
  rc = xfce_rc_simple_open (info_path, TRUE);
  if (G_LIKELY (rc != NULL))
    {
      xfce_rc_set_group (rc, "Trash Info");
      path = xfce_rc_read_entry (rc, "Path", NULL);
      date = xfce_rc_read_entry (rc, "DeletionDate", NULL);
      if (G_LIKELY (path != NULL && date != NULL))
        info = thunar_vfs_trash_info_new (path, date);
      xfce_rc_close (rc);
    }
  g_free (info_path);

  return info;
}



/**
 * thunar_vfs_trash_get_path:
 * @trash : a #ThunarVfsTrash.
 * @file  : the basename of the trashed file.
 *
 * Returns the real absolute path to the @file in @trash. Call
 * #g_free() on the returned path if you are done with it.
 *
 * Return value: the absolute path to @file in @trash.
 **/
gchar*
thunar_vfs_trash_get_path (ThunarVfsTrash *trash,
                           const gchar    *file)
{
  g_return_val_if_fail (THUNAR_VFS_IS_TRASH (trash), NULL);
  g_return_val_if_fail (file != NULL && strchr (file, '/') == NULL, NULL);

  return g_build_filename (trash->files_directory, file, NULL);
}



/**
 * thunar_vfs_trash_get_uri:
 * @trash : a #ThunarVfsTrash.
 * @file  : the basename of the trashed file.
 *
 * Generates a 'trash://' URI that refers to the @file in @trash.
 *
 * You'll need to call #thunar_vfs_uri_unref() on the returned
 * object when you are done with it.
 *
 * Return value: the generated #ThunarVfsURI.
 **/
ThunarVfsURI*
thunar_vfs_trash_get_uri (ThunarVfsTrash *trash,
                          const gchar    *file)
{
  ThunarVfsURI *uri;
  gchar        *identifier;

  g_return_val_if_fail (THUNAR_VFS_IS_TRASH (trash), NULL);
  g_return_val_if_fail (file != NULL && strchr (file, '/') == NULL, NULL);

  identifier = g_strdup_printf ("trash:///%d-%s", trash->id, file);
  uri = thunar_vfs_uri_new (identifier, NULL);
  g_free (identifier);

  return uri;
}




enum
{
  THUNAR_VFS_TRASH_MANAGER_PROP_0,
  THUNAR_VFS_TRASH_MANAGER_PROP_EMPTY,
};



static void thunar_vfs_trash_manager_class_init     (ThunarVfsTrashManagerClass *klass);
static void thunar_vfs_trash_manager_init           (ThunarVfsTrashManager      *manager);
static void thunar_vfs_trash_manager_finalize       (GObject                    *object);
static void thunar_vfs_trash_manager_get_property   (GObject                    *object,
                                                     guint                       prop_id,
                                                     GValue                     *value,
                                                     GParamSpec                 *pspec);
static void thunar_vfs_trash_manager_notify_files   (ThunarVfsTrash             *trash,
                                                     GParamSpec                 *pspec,
                                                     ThunarVfsTrashManager      *manager);



struct _ThunarVfsTrashManagerClass
{
  GObjectClass __parent__;
};

struct _ThunarVfsTrashManager
{
  GObject __parent__;

  GList *trashes;
};



G_DEFINE_TYPE (ThunarVfsTrashManager, thunar_vfs_trash_manager, G_TYPE_OBJECT);



static void
thunar_vfs_trash_manager_class_init (ThunarVfsTrashManagerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = thunar_vfs_trash_manager_finalize;
  gobject_class->get_property = thunar_vfs_trash_manager_get_property;

  /**
   * ThunarVfsTrashManager:empty:
   *
   * %TRUE if all trashes handled by the given trash manager are
   * empty, else %FALSE.
   **/
  g_object_class_install_property (gobject_class,
                                   THUNAR_VFS_TRASH_MANAGER_PROP_EMPTY,
                                   g_param_spec_boolean ("empty",
                                                         _("Empty"),
                                                         _("Whether all trashes are empty"),
                                                         TRUE,
                                                         EXO_PARAM_READABLE));
}



static void
thunar_vfs_trash_manager_init (ThunarVfsTrashManager *manager)
{
  ThunarVfsTrash *trash;
  gchar          *trash_dir;

  /* add the always present "home trash" */
  trash_dir = xfce_resource_save_location (XFCE_RESOURCE_DATA, "Trash", TRUE);
  trash = thunar_vfs_trash_new_internal (trash_dir);
  manager->trashes = g_list_prepend (manager->trashes, trash);
  g_signal_connect (G_OBJECT (trash), "notify::files", G_CALLBACK (thunar_vfs_trash_manager_notify_files), manager);
}



static void
thunar_vfs_trash_manager_finalize (GObject *object)
{
  ThunarVfsTrashManager *manager = THUNAR_VFS_TRASH_MANAGER (object);

  /* free all trashes */
  g_list_foreach (manager->trashes, (GFunc) g_object_unref, NULL);
  g_list_free (manager->trashes);

  G_OBJECT_CLASS (thunar_vfs_trash_manager_parent_class)->finalize (object);
}



static void
thunar_vfs_trash_manager_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  ThunarVfsTrashManager *manager = THUNAR_VFS_TRASH_MANAGER (object);

  switch (prop_id)
    {
    case THUNAR_VFS_TRASH_MANAGER_PROP_EMPTY:
      g_value_set_boolean (value, thunar_vfs_trash_manager_is_empty (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
thunar_vfs_trash_manager_notify_files (ThunarVfsTrash        *trash,
                                       GParamSpec            *pspec,
                                       ThunarVfsTrashManager *manager)
{
  g_return_if_fail (THUNAR_VFS_IS_TRASH (trash));
  g_return_if_fail (THUNAR_VFS_IS_TRASH_MANAGER (manager));

  g_object_notify (G_OBJECT (manager), "empty");
}



/**
 * thunar_vfs_trash_manager_get_default:
 *
 * Returns the default trash manager instance, which is shared by all
 * modules using the trash implementation. You'll need to call
 * #g_object_unref() on the returned object when you're done with it.
 *
 * Return value: the default trash manager.
 **/
ThunarVfsTrashManager*
thunar_vfs_trash_manager_get_default (void)
{
  static ThunarVfsTrashManager *manager = NULL;

  if (G_UNLIKELY (manager == NULL))
    {
      manager = g_object_new (THUNAR_VFS_TYPE_TRASH_MANAGER, NULL);
      g_object_add_weak_pointer (G_OBJECT (manager), (gpointer) &manager);
    }
  else
    {
      g_object_ref (G_OBJECT (manager));
    }

  return manager;
}



/**
 * thunar_vfs_trash_manager_is_empty:
 * @manager : a #ThunarVfsTrashManager.
 *
 * Determines whether all trashes handled by @manager are
 * empty. Returns %FALSE if atleast one trash contains
 * files, else %TRUE.
 *
 * Return value: %TRUE if @manager has no files, else %FALSE.
 **/
gboolean
thunar_vfs_trash_manager_is_empty (ThunarVfsTrashManager *manager)
{
  GList *lp;

  g_return_val_if_fail (THUNAR_VFS_IS_TRASH_MANAGER (manager), TRUE);

  for (lp = manager->trashes; lp != NULL; lp = lp->next)
    if (thunar_vfs_trash_get_files (THUNAR_VFS_TRASH (lp->data)) != NULL)
      return FALSE;

  return TRUE;
}



/**
 * thunar_vfs_trash_manager_get_trashes:
 * @manager : a #ThunarVfsTrashManager.
 *
 * Returns all trashes currently known to the @manager.
 *
 * The returned list must be freed when no longer needed,
 * which can be done like this:
 * <informalexample><programlisting>
 * g_list_foreach (list, (GFunc) g_object_unref, NULL);
 * g_list_free (list);
 * </programlisting></informalexample>
 *
 * Return value: the list of currently known trashes.
 **/
GList*
thunar_vfs_trash_manager_get_trashes (ThunarVfsTrashManager *manager)
{
  GList *result = NULL;
  GList *lp;

  g_return_val_if_fail (THUNAR_VFS_IS_TRASH_MANAGER (manager), NULL);
  
  /* we duplicate the list here, in order to be able to change
   * the relation between the trash manager and the trashes later
   * without breaking all other modules.
   */
  for (lp = manager->trashes; lp != NULL; lp = lp->next)
    result = g_list_prepend (result, g_object_ref (G_OBJECT (lp->data)));

  return result;
}



/**
 * thunar_vfs_trash_manager_resolve_uri:
 * @manager :  a #ThunarVfsTrashManager.
 * @uri     : the #ThunarVfsURI to resolve.
 * @path    : location to store the relative path to.
 * @error   : return location for errors or %NULL.
 *
 * Parses the given @uri and determines the trash referenced by
 * the @uri and the relative path of the file within the trash.
 *
 * Returns the #ThunarVfsTrash referenced by @uri or %NULL on
 * error, in which case @error will point to a description
 * of the exact cause.
 *
 * For example, the URI 'trash:///0-bar/foo' referes to the
 * file 'foo' in 'bar' within the first trash, which is usually
 * the "home trash". So a reference to the "home trash" will
 * be returned and @path will be set to 'bar/foo'.
 *
 * It is a bug to pass the root trash URI 'trash:///' to
 * this function.
 *
 * You'll need to call #g_object_unref() on the returned
 * object when you are done with it. In addition if you
 * did not specify %NULL for @path, you'll also have to
 * #g_free() the returned path.
 *
 * Return value: the #ThunarVfsTrash or %NULL on error.
 **/
ThunarVfsTrash*
thunar_vfs_trash_manager_resolve_uri (ThunarVfsTrashManager *manager,
                                      ThunarVfsURI          *uri,
                                      gchar                **path,
                                      GError               **error)
{
  ThunarVfsTrash *trash = NULL;
  const gchar    *uri_path;
  GList          *lp;
  gchar          *p;
  gint            id;

  g_return_val_if_fail (THUNAR_VFS_IS_TRASH_MANAGER (manager), NULL);
  g_return_val_if_fail (THUNAR_VFS_IS_URI (uri), NULL);
  g_return_val_if_fail (thunar_vfs_uri_get_scheme (uri) == THUNAR_VFS_URI_SCHEME_TRASH, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  /* query the path of the uri, skipping the leading '/' */
  uri_path = thunar_vfs_uri_get_path (uri) + 1;

  /* be sure to reset errno prior to calling strtoul() */
  errno = 0;

  /* try to extract the trash id */
  id = (gint) strtoul (uri_path, &p, 10);
  if (errno != 0 || p[0] != '-' || p[1] == '\0' || p[1] == '/')
    {
      p = thunar_vfs_uri_to_string (uri, 0);
      g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI,
                   "Unable to parse malformed trash URI `%s'", p);
      g_free (p);
      return NULL;
    }

  /* lookup a matching trash */
  for (lp = manager->trashes; lp != NULL; lp = lp->next)
    if (THUNAR_VFS_TRASH (lp->data)->id == id)
      {
        trash = THUNAR_VFS_TRASH (lp->data);
        break;
      }

  /* verify that a trash was found */
  if (G_UNLIKELY (trash == NULL))
    {
      g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI,
                   "Invalid trash id %d", id);
      return NULL;
    }

  /* copy the path for the caller */
  if (path != NULL)
    *path = g_strdup (p + 1);

  /* take a reference for the caller */
  g_object_ref (G_OBJECT (trash));

  /* and return the trash */
  return trash;
}


