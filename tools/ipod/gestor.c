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


/* Tipo de archivo segun extension. NULL = formato no soportado por el iPod Touch 2G */
static const char *tipo_por_extension(const char *file) {
    const char *dot = strrchr(file, '.');
    if (!dot) return NULL;
    if (!g_ascii_strcasecmp(dot, ".mp3")) return "MPEG audio file";
    if (!g_ascii_strcasecmp(dot, ".m4a") || !g_ascii_strcasecmp(dot, ".aac") ||
        !g_ascii_strcasecmp(dot, ".m4b")) return "AAC audio file";
    return NULL;
}

/* Corta una linea en campos separados por TAB (modifica la cadena). Devuelve cuantos campos hay. */
static int separar_tabs(char *linea, char **campos, int max) {
    int n = 0;
    char *p = linea;
    while (n < max) {
        campos[n++] = p;
        char *t = strchr(p, '\t');
        if (!t) break;
        *t = '\0';
        p = t + 1;
    }
    return n;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "Uso:\n"
            "  %s <mountpoint> list            (dbid<TAB>artista<TAB>titulo)\n"
            "  %s <mountpoint> add <archivo.mp3> [titulo] [artista] [duracion_ms]\n"
            "  %s <mountpoint> addmany      (lee stdin: archivo TAB titulo TAB artista TAB dur_ms TAB album TAB genero TAB bitrate TAB anio TAB pista)\n"
            "  %s <mountpoint> del <dbid> [dbid ...]\n"
            "  %s <mountpoint> settags      (lee stdin: dbid TAB titulo TAB artista TAB album TAB dur_ms; campo vacio = no cambiar)\n"
            "  %s <mountpoint> setart       (lee stdin: dbid TAB imagen.jpg  |  dbid TAB -  para quitar la caratula)\n"
            "  %s <mountpoint> plnew <nombre>\n"
            "  %s <mountpoint> plrename <nombre_actual> <nombre_nuevo>\n"
            "  %s <mountpoint> pldel <nombre> [nombre ...]\n"
            "  (addmany acepta un nombre de lista opcional: addmany <lista>)\n", argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
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

    } else if (!strcmp(cmd, "addmany")) {
        char linea[8192];
        int ok = 0, fallos = 0;
        Itdb_Playlist *destino = NULL;
        if (argc >= 4 && argv[3][0]) {
            destino = itdb_playlist_by_name(db, argv[3]);
            if (!destino || itdb_playlist_is_mpl(destino)) {
                fprintf(stderr, "No existe la lista '%s' (no se anadio nada)\n", argv[3]);
                return 1;
            }
        }
        while (fgets(linea, sizeof linea, stdin)) {
            size_t len = strlen(linea);
            while (len && (linea[len-1] == '\n' || linea[len-1] == '\r')) linea[--len] = '\0';
            if (!len) continue;

            char *c[9] = {0};
            int nc = separar_tabs(linea, c, 9);
            const char *file = c[0];
            const char *ft = tipo_por_extension(file);
            if (!ft) {
                printf("FAIL\t%s\tformato no soportado (solo mp3/m4a/aac)\n", file);
                fflush(stdout); fallos++;
                continue;
            }

            gchar *base = g_path_get_basename(file);
            Itdb_Track *t = itdb_track_new();
            t->title  = g_strdup((nc > 1 && c[1][0]) ? c[1] : base);
            if (nc > 2 && c[2][0]) t->artist = g_strdup(c[2]);   /* sin artista: se deja vacio */
            t->tracklen = (nc > 3) ? atoi(c[3]) : 0;
            if (nc > 4 && c[4][0]) t->album = g_strdup(c[4]);
            if (nc > 5 && c[5][0]) t->genre = g_strdup(c[5]);
            if (nc > 6 && atoi(c[6]) > 0) t->bitrate = atoi(c[6]);
            if (nc > 7 && atoi(c[7]) > 0) t->year = atoi(c[7]);
            if (nc > 8 && atoi(c[8]) > 0) t->track_nr = atoi(c[8]);
            t->mediatype = ITDB_MEDIATYPE_AUDIO;
            t->filetype = g_strdup(ft);
            t->samplerate = 44100;
            g_free(base);

            itdb_track_add(db, t, -1);
            itdb_playlist_add_track(itdb_playlist_mpl(db), t, -1);

            GError *err = NULL;
            if (!itdb_cp_track_to_ipod(t, file, &err)) {
                printf("FAIL\t%s\t%s\n", file, err ? err->message : "error al copiar");
                fflush(stdout);
                itdb_playlist_remove_track(itdb_playlist_mpl(db), t);
                itdb_track_remove(t);
                fallos++;
                continue;
            }
            if (destino) itdb_playlist_add_track(destino, t, -1);
            printf("COPIED\t%s\n", file);
            fflush(stdout);
            ok++;
        }
        if (ok > 0) guardar(db);
        printf("RESUMEN\t%d\t%d\n", ok, fallos);
        fflush(stdout);
        itdb_free(db);
        return ok > 0 ? 0 : 1;

    } else if (!strcmp(cmd, "settags")) {
        char linea[8192];
        int cambiados = 0, fallos = 0;
        while (fgets(linea, sizeof linea, stdin)) {
            size_t len = strlen(linea);
            while (len && (linea[len-1] == '\n' || linea[len-1] == '\r')) linea[--len] = '\0';
            if (!len) continue;

            char *c[5] = {0};
            int nc = separar_tabs(linea, c, 5);
            guint64 id = g_ascii_strtoull(c[0], NULL, 10);
            Itdb_Track *t = NULL;
            if (id) {
                for (GList *l = db->tracks; l; l = l->next)
                    if (((Itdb_Track *)l->data)->dbid == id) { t = l->data; break; }
            }
            if (!t) {
                printf("FAIL\t%s\tno existe una cancion con ese id\n", c[0]);
                fflush(stdout); fallos++;
                continue;
            }
            if (nc > 1 && c[1][0]) { g_free(t->title);  t->title  = g_strdup(c[1]); g_free(t->sort_title);  t->sort_title  = NULL; }
            if (nc > 2 && c[2][0]) { g_free(t->artist); t->artist = g_strdup(c[2]); g_free(t->sort_artist); t->sort_artist = NULL; }
            if (nc > 3 && c[3][0]) { g_free(t->album);  t->album  = g_strdup(c[3]); g_free(t->sort_album);  t->sort_album  = NULL; }
            if (nc > 4 && atoi(c[4]) > 0) t->tracklen = atoi(c[4]);
            printf("UPDATED\t%s\n", c[0]);
            fflush(stdout);
            cambiados++;
        }
        if (cambiados > 0) guardar(db);
        printf("RESUMEN\t%d\t%d\n", cambiados, fallos);
        fflush(stdout);
        itdb_free(db);
        return cambiados > 0 ? 0 : 1;

    } else if (!strcmp(cmd, "setart")) {
        if (!itdb_device_supports_artwork(db->device)) {
            fprintf(stderr, "Error: este dispositivo no admite caratulas (falta SysInfoExtended?)\n");
            return 1;
        }
        char linea[8192];
        int cambiados = 0, fallos = 0;
        while (fgets(linea, sizeof linea, stdin)) {
            size_t len = strlen(linea);
            while (len && (linea[len-1] == '\n' || linea[len-1] == '\r')) linea[--len] = '\0';
            if (!len) continue;

            char *c[2] = {0};
            int nc = separar_tabs(linea, c, 2);
            guint64 id = g_ascii_strtoull(c[0], NULL, 10);
            Itdb_Track *t = NULL;
            if (id) {
                for (GList *l = db->tracks; l; l = l->next)
                    if (((Itdb_Track *)l->data)->dbid == id) { t = l->data; break; }
            }
            if (!t) {
                printf("FAIL\t%s\tno existe una cancion con ese id\n", c[0]);
                fflush(stdout); fallos++;
                continue;
            }
            if (nc < 2 || !c[1][0]) {
                printf("FAIL\t%s\tfalta la imagen\n", c[0]);
                fflush(stdout); fallos++;
                continue;
            }
            if (!strcmp(c[1], "-")) {
                itdb_track_remove_thumbnails(t);
            } else if (!itdb_track_set_thumbnails(t, c[1])) {
                printf("FAIL\t%s\tno se pudo usar la imagen %s\n", c[0], c[1]);
                fflush(stdout); fallos++;
                continue;
            }
            printf("UPDATED\t%s\n", c[0]);
            fflush(stdout);
            cambiados++;
        }
        if (cambiados > 0) guardar(db);
        printf("RESUMEN\t%d\t%d\n", cambiados, fallos);
        fflush(stdout);
        itdb_free(db);
        return cambiados > 0 ? 0 : 1;

    } else if (!strcmp(cmd, "plnew") && argc >= 4) {
        if (!argv[3][0] || itdb_playlist_by_name(db, argv[3])) {
            fprintf(stderr, "Nombre invalido o ya existe una lista llamada '%s'\n", argv[3]);
            return 1;
        }
        Itdb_Playlist *pl = itdb_playlist_new(argv[3], FALSE);
        itdb_playlist_add(db, pl, -1);
        printf("Lista creada: %s\n", argv[3]);
        guardar(db);

    } else if (!strcmp(cmd, "plrename") && argc >= 5) {
        Itdb_Playlist *pl = itdb_playlist_by_name(db, argv[3]);
        if (!pl || itdb_playlist_is_mpl(pl) || itdb_playlist_is_podcasts(pl)) {
            fprintf(stderr, "No se puede renombrar '%s' (no existe o es una lista del sistema)\n", argv[3]);
            return 1;
        }
        if (!argv[4][0] || itdb_playlist_by_name(db, argv[4])) {
            fprintf(stderr, "Nombre nuevo invalido o ya existe: '%s'\n", argv[4]);
            return 1;
        }
        g_free(pl->name);
        pl->name = g_strdup(argv[4]);
        guardar(db);

    } else if (!strcmp(cmd, "pldel") && argc >= 4) {
        int n = argc - 3;
        Itdb_Playlist **pls = g_new0(Itdb_Playlist *, n);
        for (int k = 0; k < n; k++) {
            pls[k] = itdb_playlist_by_name(db, argv[3 + k]);
            if (!pls[k] || itdb_playlist_is_mpl(pls[k]) || itdb_playlist_is_podcasts(pls[k])) {
                fprintf(stderr, "No se puede borrar '%s' (no existe o es una lista del sistema); no se borro nada\n", argv[3 + k]);
                return 1;
            }
            for (int j = 0; j < k; j++) {
                if (pls[j] == pls[k]) {
                    fprintf(stderr, "Lista repetida: '%s'; no se borro nada\n", argv[3 + k]);
                    return 1;
                }
            }
        }
        for (int k = 0; k < n; k++) {
            printf("Borrando lista: %s\n", pls[k]->name);
            itdb_playlist_remove(pls[k]);   /* solo la lista; las canciones quedan en la biblioteca */
        }
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
