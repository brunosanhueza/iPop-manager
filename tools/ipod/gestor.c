#include <gpod/itdb.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Itdb_iTunesDB *abrir(const char *mp) {
    GError *err = NULL;
    Itdb_iTunesDB *db = itdb_parse(mp, &err);
    if (!db) {
        fprintf(stderr, "Error al leer: %s\n", err ? err->message : "desconocido");
        exit(1);
    }
    return db;
}

static void guardar(Itdb_iTunesDB *db) {
    GError *err = NULL;
    if (!itdb_write(db, &err)) {
        fprintf(stderr, "Error al escribir: %s\n", err ? err->message : "desconocido");
        exit(1);
    }
    printf("Base de datos escrita OK\n");
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "Uso:\n"
            "  %s <mountpoint> list            (dbid<TAB>artista<TAB>titulo)\n"
            "  %s <mountpoint> add <archivo.mp3> [titulo] [artista] [duracion_ms]\n"
            "  %s <mountpoint> del <dbid> [dbid ...]\n", argv[0], argv[0], argv[0]);
        return 1;
    }
    const char *mp = argv[1];
    const char *cmd = argv[2];
    Itdb_iTunesDB *db = abrir(mp);

    if (!strcmp(cmd, "list")) {
        int n = 0;
        for (GList *l = db->tracks; l; l = l->next, n++) {
            Itdb_Track *t = l->data;
            printf("%" G_GUINT64_FORMAT "\t%s\t%s\n", t->dbid,
                   t->artist ? t->artist : "?", t->title ? t->title : "?");
        }
        fprintf(stderr, "Total: %d\n", n);

    } else if (!strcmp(cmd, "add") && argc >= 4) {
        const char *file = argv[3];
        gchar *base = g_path_get_basename(file);
        Itdb_Track *t = itdb_track_new();
        t->title = g_strdup(argc >= 5 ? argv[4] : base);
        t->artist = g_strdup(argc >= 6 ? argv[5] : "Desconocido");
        t->tracklen = argc >= 7 ? atoi(argv[6]) : 0;
        t->mediatype = ITDB_MEDIATYPE_AUDIO;
        t->filetype = g_strdup("MPEG audio file");
        t->samplerate = 44100;
        g_free(base);

        itdb_track_add(db, t, -1);
        itdb_playlist_add_track(itdb_playlist_mpl(db), t, -1);

        GError *err = NULL;
        if (!itdb_cp_track_to_ipod(t, file, &err)) {
            fprintf(stderr, "Error al copiar: %s\n", err ? err->message : "desconocido");
            itdb_track_remove(t);
            return 1;
        }
        printf("Copiado a: %s\n", t->ipod_path);
        guardar(db);

    } else if (!strcmp(cmd, "del") && argc >= 4) {
        int n = argc - 3;
        Itdb_Track **tr = g_new0(Itdb_Track *, n);
        gchar **paths = g_new0(gchar *, n);

        /* 1. resolver TODOS los ids antes de tocar nada */
        for (int k = 0; k < n; k++) {
            const char *arg = argv[3 + k];
            char *fin = NULL;
            guint64 id = g_ascii_strtoull(arg, &fin, 10);
            if (!*arg || *fin != '\0' || id == 0) {
                fprintf(stderr, "ID invalido: '%s' (no se borro nada)\n", arg);
                return 1;
            }
            for (GList *l = db->tracks; l; l = l->next) {
                Itdb_Track *c = l->data;
                if (c->dbid == id) { tr[k] = c; break; }
            }
            if (!tr[k]) {
                fprintf(stderr, "No existe cancion con id %s (no se borro nada)\n", arg);
                return 1;
            }
            for (int j = 0; j < k; j++) {
                if (tr[j] == tr[k]) {
                    fprintf(stderr, "ID repetido: %s (no se borro nada)\n", arg);
                    return 1;
                }
            }
        }

        /* 2. guardar rutas, sacar de todas las listas y de la base */
        for (int k = 0; k < n; k++) {
            printf("Borrando: %s\n", tr[k]->title ? tr[k]->title : "?");
            paths[k] = itdb_filename_on_ipod(tr[k]);
        }
        for (int k = 0; k < n; k++) {
            GList *copia = g_list_copy(db->playlists);
            for (GList *p = copia; p; p = p->next) {
                Itdb_Playlist *pl = p->data;
                if (itdb_playlist_contains_track(pl, tr[k]))
                    itdb_playlist_remove_track(pl, tr[k]);
            }
            g_list_free(copia);
            itdb_track_remove(tr[k]);
        }

        /* 3. una sola escritura, y recien despues los mp3 */
        guardar(db);
        for (int k = 0; k < n; k++) {
            if (paths[k]) {
                g_unlink(paths[k]);
                g_free(paths[k]);
            }
        }

    } else {
        fprintf(stderr, "Comando invalido\n");
        return 1;
    }

    itdb_free(db);
    return 0;
}
