/***************************************************************
    Namma Metro — Professional Route Finder
    Clean, Commented, Student-Friendly Version

    Features:
    - UTF-8 safe (em-dash, arrows, emojis)
    - BFS shortest path for routes
    - Alternate route suggestions (by blocking edges)
    - Autocomplete station suggestions
    - Pretty terminal UI with colors + simple table layout
    - TXT + HTML route report export
    - Cross-platform "open in default app"
    - Planned station toggle (future-ready)

    NOTE FOR WINDOWS USERS:
    - This program automatically sets console to UTF-8 using
      SetConsoleOutputCP(CP_UTF8) and SetConsoleCP(CP_UTF8).
****************************************************************/

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// =============================================================
// CONSTANTS & METRO PARAMETERS
// =============================================================
#define MAX 400                     // Max stations
#define AVG_KM_PER_EDGE 1.1         // Approx. km between stations
#define TIME_PER_EDGE_MIN 2         // Travel time between adjacent stations (minutes)
#define INTERCHANGE_TIME_MIN 3      // Extra time when you change lines
#define MAX_ALTERNATES 3            // Max alternate routes

// =============================================================
// TERMINAL COLORS (ANSI ESCAPE CODES)
// =============================================================
// These work in most terminals that support ANSI and UTF-8.
#define CLR_RESET  "\x1b[0m"
#define CLR_BOLD   "\x1b[1m"
#define CLR_DIM    "\x1b[2m"
#define CLR_PURPLE "\x1b[35m"
#define CLR_GREEN  "\x1b[32m"
#define CLR_PINK   "\x1b[95m"
#define CLR_CYAN   "\x1b[36m"
#define CLR_YELLOW "\x1b[33m"

// =============================================================
// STATION STRUCTURE
// =============================================================
/*
    display_name : Human-friendly name (e.g., "M.G. Road")
    key_name     : Normalized, lowercase name used for matching
    line_count   : Number of lines that pass via this station
    lines        : Names of lines (e.g., "purple", "green", "pink")
    planned      : 0 = open, 1 = planned (future/under construction)
*/
typedef struct {
    char display_name[80];
    char key_name[80];
    int  line_count;
    char lines[6][30];
    int  planned;
} Station;

// Global data
Station stations[MAX];
int stationCount = 0;
int adj[MAX][MAX];   // adjacency matrix: 1 if two stations are connected

// =============================================================
// STRING UTILITIES
// =============================================================

// Convert string to lowercase (in-place)
void lowercase(char *s) {
    for (int i = 0; s[i]; i++) {
        s[i] = (char)tolower((unsigned char)s[i]);
    }
}

/*
    normalize_inplace()

    Goal:
      Convert user input station names into a normalized key
      so that variations still match.

    Steps:
      1. Keep only letters, digits, spaces.
      2. Collapse multiple spaces into a single space.
      3. Combine single-letter tokens like:
         "m g road" -> "mg road"
      4. Convert to lowercase.

    So all of these become the same key:
      "M.G. Road"
      "mg road"
      "M G ROAD"
*/
void normalize_inplace(char *s) {
    char tmp[80];
    int j = 0;

    // Step 1: remove punctuation (keep alnum + spaces)
    for (int i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || isspace(c)) {
            tmp[j++] = (char)c;
        }
    }
    tmp[j] = '\0';

    // Step 2: collapse multiple spaces
    char tmp2[80];
    int k = 0;
    int i = 0;

    // skip leading spaces
    while (tmp[i] && isspace((unsigned char)tmp[i])) i++;

    int last_space = 0;
    for (; tmp[i]; i++) {
        if (isspace((unsigned char)tmp[i])) {
            if (!last_space) {
                tmp2[k++] = ' ';
                last_space = 1;
            }
        } else {
            tmp2[k++] = tmp[i];
            last_space = 0;
        }
    }

    // remove trailing space if any
    if (k > 0 && tmp2[k - 1] == ' ')
        k--;

    tmp2[k] = '\0';

    // Step 3: combine sequences like "m g" -> "mg"
    char tmp3[80];
    int p = 0;
    i = 0;

    while (tmp2[i]) {
        if (isalpha((unsigned char)tmp2[i]) &&
            tmp2[i + 1] == ' ' &&
            isalpha((unsigned char)tmp2[i + 2])) {

            // copy first letter, then second letter, skip the space
            tmp3[p++] = tmp2[i];
            tmp3[p++] = tmp2[i + 2];
            i += 3;
            continue;
        }
        tmp3[p++] = tmp2[i++];
    }
    tmp3[p] = '\0';

    // Step 4: lowercase everything
    lowercase(tmp3);

    // copy back to original
    strncpy(s, tmp3, 79);
    s[79] = '\0';
}

// =============================================================
// FARE CALCULATION (DISTANCE-BASED SLABS)
// =============================================================
int fare_from_distance(double km) {
    if (km <= 2)  return 10;
    if (km <= 4)  return 20;
    if (km <= 6)  return 30;
    if (km <= 8)  return 40;
    if (km <= 10) return 50;
    if (km <= 15) return 60;
    if (km <= 20) return 70;
    if (km <= 25) return 80;
    return 90;
}

// =============================================================
// STATION AND NETWORK BUILDING
// =============================================================

/*
    find_or_add_by_key_with_plan(key, display, planned)

    If a station with normalized key already exists:
        - return its ID
    Otherwise:
        - create new station,
        - store key_name + cleaned display_name,
        - store planned flag,
        - return new ID
*/
int find_or_add_by_key_with_plan(const char *key, const char *display, int planned) {
    // check if station exists
    for (int i = 0; i < stationCount; i++) {
        if (strcmp(stations[i].key_name, key) == 0) {
            return i;
        }
    }

    // create new
    strncpy(stations[stationCount].key_name, key, 79);
    stations[stationCount].key_name[79] = '\0';

    // trim spaces in display name
    char tmp[80];
    strncpy(tmp, display, 79);
    tmp[79] = '\0';

    int s = 0;
    while (tmp[s] && isspace((unsigned char)tmp[s])) s++;

    int e = (int)strlen(tmp) - 1;
    while (e >= 0 && isspace((unsigned char)tmp[e])) e--;

    if (s <= e) {
        int len = e - s + 1;
        if (len > 79) len = 79;
        strncpy(stations[stationCount].display_name, tmp + s, len);
        stations[stationCount].display_name[len] = '\0';
    } else {
        stations[stationCount].display_name[0] = '\0';
    }

    stations[stationCount].line_count = 0;
    stations[stationCount].planned = planned;

    return stationCount++;
}

// Attach a line name (like "purple") to a station
void add_line_tag(int id, const char *line) {
    for (int i = 0; i < stations[id].line_count; i++) {
        if (strcmp(stations[id].lines[i], line) == 0) {
            return; // already present
        }
    }
    strncpy(stations[id].lines[stations[id].line_count], line, 29);
    stations[id].lines[stations[id].line_count][29] = '\0';
    stations[id].line_count++;
}

// Connect two station IDs in adjacency matrix
void connect_ids(int a, int b) {
    if (a < 0 || b < 0 || a == b) return;
    adj[a][b] = 1;
    adj[b][a] = 1;
}

/*
    add_line_with_plan(lineName, list, n, planned_flags)

    For each station name in the list:
      - normalize to key
      - find or create station
      - tag with lineName
    Then connect them sequentially as edges.
*/
void add_line_with_plan(const char *lineName, const char *list[], int n, int planned_flags[]) {
    int ids[MAX];

    for (int i = 0; i < n; i++) {
        char display[80];
        char key[80];

        strncpy(display, list[i], 79);
        display[79] = '\0';

        strncpy(key, list[i], 79);
        key[79] = '\0';
        normalize_inplace(key);

        ids[i] = find_or_add_by_key_with_plan(key, display,
                                              planned_flags ? planned_flags[i] : 0);
        add_line_tag(ids[i], lineName);
    }

    // connect consecutive stations in this line
    for (int i = 0; i < n - 1; i++) {
        connect_ids(ids[i], ids[i + 1]);
    }
}

/*
    build_network(include_planned)

    Rebuilds the entire graph from scratch.
    For now, all stations have planned flag 0.
    In future, you can mark some as planned and
    toggle them using include_planned.
*/
void build_network(int include_planned) {
    stationCount = 0;

    // reset adjacency matrix
    for (int i = 0; i < MAX; i++)
        for (int j = 0; j < MAX; j++)
            adj[i][j] = 0;

    // =======================
    // PURPLE LINE
    // =======================
    const char *purple[] = {
        "challaghatta", "kengeri","Kengeri Bus Terminal","Pattanagere","Jnanbharati","Rajarajeshwari Nagar",
        "Nayandahalli","mysore road", "deepanjali nagar", "attiguppe",
        "vijayanagar", "Hosahalli", "magadi road", "majestic", "Central Road","Vidhana Soudha",
        "Cubbon Park","m.g. road", "trinity", "halasuru", "indiranagar",
        "swami vivekananda road", "baiyappanahalli","Benniganahalli", "kr puram","Singayyanapalya","Garudacharpalaya",
        "hoodi","Seetharampalya","Kundalahalli","Nallurhalli","Sri Satya Sai Hospital","Pattandur Agrahara","Kadugodi Tree Park",
        "Channasandra(HopeFarm)", "whitefield(Kadugodi)"
    };
    int purple_planned[sizeof(purple) / sizeof(purple[0])];
    for (int i = 0; i < (int)(sizeof(purple) / sizeof(purple[0])); i++)
        purple_planned[i] = 0;

    // =======================
    // GREEN LINE
    // =======================
    const char *green[] = {
        "Madavara", "Chikkabidarakallu", "Manjunathanagar", "nagasandra", "Dasarhalli","Jalahalli",
        "Peenya Industry", "Peenya", "Gorguntepalya", "Yeswantpur", "Sandal Soap Factory", "Mahalakshmi",
        "Rajijnagar", "Kuvempu road", "Srirampura", "Sampige Road", "majestic",
        "Chickpete", "Krishna Rajendra Market", "National College", "Lalbagh", "South End Circle",
        "Jayanagar", "Rashtreeya Vidyalaya Road", "Banashankari","jayadeva hospital", "Yelachenahalli", "Konanakunte Cross",
        "Vajarahalli","Thalaghattapura","Silk Institute"
    };
    int green_planned[sizeof(green) / sizeof(green[0])];
    for (int i = 0; i < (int)(sizeof(green) / sizeof(green[0])); i++)
        green_planned[i] = 0;

    // =======================
    // PINK LINE
    // =======================
    const char *pink[] = {
        "kalena agrahara", "hulimavu", "iim bangalore", "jp nagar 4th phase",
        "jayadeva hospital", "Tavarekere","dairy circle", "lakkasandra", "langford town",
        "rashtriya military school", "mg road", "shivajinagar", "Cantonment","Pottery Town",
        "tannery road","Venkateshpura","kadugundanahalli", "nagawara"
    };
    int pink_planned[sizeof(pink) / sizeof(pink[0])];
    for (int i = 0; i < (int)(sizeof(pink) / sizeof(pink[0])); i++)
        pink_planned[i] = 0;

    // add lines to graph
    add_line_with_plan("purple", purple, (int)(sizeof(purple) / sizeof(purple[0])), purple_planned);
    add_line_with_plan("green",  green,  (int)(sizeof(green)  / sizeof(green[0])),  green_planned);
    add_line_with_plan("pink",   pink,   (int)(sizeof(pink)   / sizeof(pink[0])),   pink_planned);

    // include_planned parameter reserved for future when some nodes are planned=1
    (void)include_planned; // silence unused warning for now
}

// =============================================================
// BFS: SHORTEST PATH (UNWEIGHTED GRAPH)
// =============================================================

/*
    bfs_with_blocked_edges(src, dest, parent, blocked_a, blocked_b, blocked_count)

    Standard BFS to find shortest path in terms of station hops.

    Algorithm:
      - Use a queue.
      - visited[] to mark visited nodes.
      - parent[] to reconstruct path later.
      - blocked edges: skip edges (u,v) that match any blocked pair.

    Returns:
      1 if path found, 0 otherwise.

    parent[v] will contain the station from which we reached v.
*/
int bfs_with_blocked_edges(
    int src,
    int dest,
    int parent[],
    int blocked_a[],
    int blocked_b[],
    int blocked_count
) {
    int visited[MAX] = {0};
    int q[MAX];
    int front = 0, rear = 0;

    // initialize BFS
    q[rear++] = src;
    visited[src] = 1;
    parent[src] = -1;

    while (front < rear) {
        int u = q[front++];

        if (u == dest) {
            return 1; // found path
        }

        for (int v = 0; v < stationCount; v++) {
            if (!adj[u][v]) continue;   // not connected
            if (visited[v]) continue;   // already visited

            // check if this edge is blocked
            int blocked = 0;
            for (int b = 0; b < blocked_count; b++) {
                if (blocked_a && blocked_b &&
                    ((u == blocked_a[b] && v == blocked_b[b]) ||
                     (u == blocked_b[b] && v == blocked_a[b]))) {
                    blocked = 1;
                    break;
                }
            }
            if (blocked) continue;

            visited[v] = 1;
            parent[v] = u;
            q[rear++] = v;
        }
    }

    return 0; // no path
}

// Simpler BFS with no blocked edges
int bfs_simple(int src, int dest, int parent[]) {
    return bfs_with_blocked_edges(src, dest, parent, NULL, NULL, 0);
}

// =============================================================
// ROUTE HELPERS
// =============================================================

// Compare two paths (arrays of indices)
int route_equals(int a[], int lena, int b[], int lenb) {
    if (lena != lenb) return 0;
    for (int i = 0; i < lena; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/*
    build_edge_lines(path, len, edgeLines)

    For each consecutive pair in the path, find the line that
    connects them (if there is one common line between the stations).
*/
void build_edge_lines(int path[], int len, char edgeLines[][30]) {
    for (int i = 0; i < len - 1; i++) {
        int a = path[i];
        int b = path[i + 1];
        int found = 0;

        for (int ai = 0; ai < stations[a].line_count && !found; ai++) {
            for (int bi = 0; bi < stations[b].line_count && !found; bi++) {
                if (strcmp(stations[a].lines[ai], stations[b].lines[bi]) == 0) {
                    strncpy(edgeLines[i], stations[a].lines[ai], 29);
                    edgeLines[i][29] = '\0';
                    found = 1;
                }
            }
        }

        if (!found) {
            strcpy(edgeLines[i], "unknown");
        }
    }
}

// =============================================================
// UI HELPERS (COLORS, EMOJIS)
// =============================================================

const char* line_color(const char *line) {
    if (strcmp(line, "purple") == 0) return CLR_PURPLE;
    if (strcmp(line, "green")  == 0) return CLR_GREEN;
    if (strcmp(line, "pink")   == 0) return CLR_PINK;
    return CLR_CYAN;
}

const char* line_emoji(const char *line) {
    if (strcmp(line, "purple") == 0) return "🟣";
    if (strcmp(line, "green")  == 0) return "🟢";
    if (strcmp(line, "pink")   == 0) return "🌸";
    return "◼️";
}

/*
    print_final_output_professional()

    CLI-only: pretty colored summary.
*/
void print_final_output_professional(int path[], int len, char edgeLines[][30]) {
    // Top header
    printf("\n%s%s%s\n", CLR_BOLD CLR_CYAN,
           "════════════════════════════════════════════════════════════════════",
           CLR_RESET);
    printf("%s%*s%s\n", CLR_BOLD, 18, "", "NAMMA METRO — ROUTE SUMMARY");
    printf("%s%s%s\n\n", CLR_BOLD CLR_CYAN,
           "════════════════════════════════════════════════════════════════════",
           CLR_RESET);

    // Route line
    printf("%s%-12s%s", CLR_BOLD, "Route:", CLR_RESET);
    for (int i = 0; i < len; i++) {
        const char *nm = stations[path[i]].display_name[0]
                         ? stations[path[i]].display_name
                         : stations[path[i]].key_name;
        printf("%s", nm);
        if (i < len - 1) {
            printf(" %s→%s ", CLR_DIM, CLR_RESET);
        }
    }
    printf("\n\n");

    // Segment table
    printf("%s%-6s | %-20s | %-20s | %-10s%s\n",
           CLR_BOLD, "Line", "Start", "End", "Segment", CLR_RESET);
    printf("%s----------------------------------------------------------------------%s\n",
           CLR_DIM, CLR_RESET);

    int i = 0;
    while (i < len - 1) {
        char curr[30];
        strncpy(curr, edgeLines[i], 29);
        curr[29] = '\0';

        int s = i;
        int e = i;

        // group consecutive edges on same line
        while (e + 1 < len - 1 && strcmp(edgeLines[e + 1], curr) == 0) {
            e++;
        }

        const char *emoji = line_emoji(curr);
        const char *color = line_color(curr);
        const char *start = stations[path[s]].display_name[0]
                            ? stations[path[s]].display_name
                            : stations[path[s]].key_name;
        const char *end   = stations[path[e + 1]].display_name[0]
                            ? stations[path[e + 1]].display_name
                            : stations[path[e + 1]].key_name;
        int seg_stops = e - s + 1;

        printf("%s%-2s %-3s%s | %-20s | %-20s | %4d stops\n",
               color, emoji, curr, CLR_RESET, start, end, seg_stops);

        i = e + 1;
    }

    // Interchanges
    printf("\n%sInterchanges:%s\n", CLR_BOLD, CLR_RESET);
    int interchanges = 0;
    for (int ii = 0; ii < len; ii++) {
        if (stations[path[ii]].line_count > 1) {
            const char *nm = stations[path[ii]].display_name[0]
                             ? stations[path[ii]].display_name
                             : stations[path[ii]].key_name;
            printf(" - %s (", nm);
            for (int j = 0; j < stations[path[ii]].line_count; j++) {
                printf("%s", stations[path[ii]].lines[j]);
                if (j + 1 < stations[path[ii]].line_count) printf(", ");
            }
            printf(")\n");
            interchanges++;
        }
    }
    if (interchanges == 0) {
        printf(" None\n");
    }

    // Per-stop breakdown
    printf("\n%sPer-stop breakdown:%s\n", CLR_BOLD, CLR_RESET);
    printf("%-28s -> %-28s | %-6s | %-6s | %-6s\n",
           "From", "To", "Dist", "Time", "Fare");
    printf("---------------------------------------------------------------------------------\n");

    int edges = len - 1;
    int total_time = 0;
    double total_dist = 0.0;

    for (int k = 0; k < edges; k++) {
        const char *a = stations[path[k]].display_name[0]
                        ? stations[path[k]].display_name
                        : stations[path[k]].key_name;
        const char *b = stations[path[k + 1]].display_name[0]
                        ? stations[path[k + 1]].display_name
                        : stations[path[k + 1]].key_name;
        double dist = AVG_KM_PER_EDGE;
        int tmin = TIME_PER_EDGE_MIN;
        int fare = fare_from_distance(dist);
        total_dist += dist;
        total_time += tmin;

        printf("%-28s -> %-28s | %5.2f | %4d m | Rs%3d\n",
               a, b, dist, tmin, fare);
    }

    int extra_interchange_time = interchanges * INTERCHANGE_TIME_MIN;
    int est_time = total_time + extra_interchange_time;
    int est_fare = fare_from_distance(total_dist);

    printf("\n%sTrip summary:%s\n", CLR_BOLD, CLR_RESET);
    printf(" - Stops travelled : %d\n", edges);
    printf(" - Distance        : %.2f km\n", total_dist);
    printf(" - Travel time     : %d min (incl. %d min interchange buffer)\n",
           est_time, extra_interchange_time);
    printf(" - Fare estimate   : Rs %d\n", est_fare);

    // current time & ETA
    time_t now = time(NULL);
    time_t arrive = now + est_time * 60;
    struct tm *tnow = localtime(&now);
    struct tm *tarr = localtime(&arrive);
    char nowbuf[40];
    char arrbuf[40];

    strftime(nowbuf, sizeof nowbuf, "%I:%M %p", tnow);
    strftime(arrbuf, sizeof arrbuf, "%I:%M %p", tarr);

    printf("\n - Current time    : %s\n", nowbuf);
    printf(" - ETA             : %s\n", arrbuf);

    printf("\n%s════════════════════════════════════════════════════════════════════%s\n\n",
           CLR_CYAN, CLR_RESET);
}

// Simple ASCII preview of the route
void print_ascii_map_preview(int path[], int len) {
    printf("%sASCII Preview:%s\n\n", CLR_BOLD, CLR_RESET);
    for (int i = 0; i < len; i++) {
        const char *n = stations[path[i]].display_name[0]
                        ? stations[path[i]].display_name
                        : stations[path[i]].key_name;
        printf("[ %s ]", n);
        if (i < len - 1) printf("==");
    }
    printf("\n\n");
}

/*
    find_alternates()

    Idea:
      - Take the primary shortest path.
      - For each edge in that path, temporarily block it.
      - Run BFS again to see if a different route exists.
      - This gives alternate paths that avoid certain segments.

    Returns:
      Number of alternate routes found.
*/
int find_alternates(int primary[], int plen,
                    int alternates[][MAX], int altlens[]) {
    int altcount = 0;

    for (int e = 0; e < plen - 1 && altcount < MAX_ALTERNATES; e++) {
        int blocked_a[1];
        int blocked_b[1];
        blocked_a[0] = primary[e];
        blocked_b[0] = primary[e + 1];

        int parent[MAX];
        for (int i = 0; i < MAX; i++) parent[i] = -1;

        if (bfs_with_blocked_edges(primary[0], primary[plen - 1],
                                   parent, blocked_a, blocked_b, 1)) {
            // reconstruct path
            int path[MAX];
            int len = 0;
            int cur = primary[plen - 1];

            while (cur != -1) {
                path[len++] = cur;
                cur = parent[cur];
            }

            // reverse
            for (int i = 0; i < len / 2; i++) {
                int t = path[i];
                path[i] = path[len - 1 - i];
                path[len - 1 - i] = t;
            }

            // check duplicates
            int dup = 0;
            if (route_equals(path, len, primary, plen)) {
                dup = 1;
            }
            for (int a = 0; a < altcount && !dup; a++) {
                if (route_equals(path, len, alternates[a], altlens[a])) {
                    dup = 1;
                }
            }

            if (!dup) {
                for (int i = 0; i < len; i++)
                    alternates[altcount][i] = path[i];
                altlens[altcount] = len;
                altcount++;
            }
        }
    }

    return altcount;
}

// =============================================================
// AUTOCOMPLETE FOR STATION NAMES
// =============================================================

void autocomplete_print(const char *prefix) {
    int printed = 0;
    int plen = (int)strlen(prefix);

    printf("\nMatches for \"%s\":\n", prefix);

    for (int i = 0; i < stationCount && printed < 20; i++) {
        if (strncmp(stations[i].key_name, prefix, plen) == 0) {
            const char *nm = stations[i].display_name[0]
                             ? stations[i].display_name
                             : stations[i].key_name;
            printf("  - %s\n", nm);
            printed++;
        }
    }

    if (printed == 0) {
        printf("  (no prefix matches)\n");
    }
}

// =============================================================
// FILE EXPORT (TXT + HTML REPORTS)
// =============================================================

void export_route_to_txt(const char *fname, int path[], int len, char edgeLines[][30]) {
    FILE *f = fopen(fname, "w");
    if (!f) {
        printf("Failed to create %s\n", fname);
        return;
    }

    time_t now = time(NULL);
    fprintf(f, "NAMMA METRO — ROUTE REPORT\nGenerated: %s\n", asctime(localtime(&now)));

    fprintf(f, "\nRoute:\n");
    for (int i = 0; i < len; i++) {
        const char *nm = stations[path[i]].display_name[0]
                         ? stations[path[i]].display_name
                         : stations[path[i]].key_name;
        fprintf(f, "%s", nm);
        if (i < len - 1) fprintf(f, " -> ");
    }

    fprintf(f, "\n\nLine segments:\n");
    int i = 0;
    while (i < len - 1) {
        char curr[30];
        strncpy(curr, edgeLines[i], 29);
        curr[29] = '\0';

        int s = i;
        int e = i;
        while (e + 1 < len - 1 && strcmp(edgeLines[e + 1], curr) == 0) {
            e++;
        }

        const char *start = stations[path[s]].display_name[0]
                            ? stations[path[s]].display_name
                            : stations[path[s]].key_name;
        const char *end   = stations[path[e + 1]].display_name[0]
                            ? stations[path[e + 1]].display_name
                            : stations[path[e + 1]].key_name;

        fprintf(f, "%s : %s -> %s\n", curr, start, end);

        for (int k = s; k <= e; k++) {
            const char *a = stations[path[k]].display_name[0]
                            ? stations[path[k]].display_name
                            : stations[path[k]].key_name;
            const char *b = stations[path[k + 1]].display_name[0]
                            ? stations[path[k + 1]].display_name
                            : stations[path[k + 1]].key_name;
            fprintf(f, "    - %s -> %s : %.2f km, %d min, slab Rs %d\n",
                    a, b, AVG_KM_PER_EDGE, TIME_PER_EDGE_MIN,
                    fare_from_distance(AVG_KM_PER_EDGE));
        }

        i = e + 1;
    }

    fprintf(f, "\nTrip summary:\n");
    int edges = len - 1;
    double total_dist = edges * AVG_KM_PER_EDGE;

    int interchanges = 0;
    for (int ii = 0; ii < len; ii++) {
        if (stations[path[ii]].line_count > 1)
            interchanges++;
    }

    int time_min = edges * TIME_PER_EDGE_MIN + interchanges * INTERCHANGE_TIME_MIN;
    int fare = fare_from_distance(total_dist);

    fprintf(f,
            " - Stops traveled: %d\n"
            " - Distance: %.2f km\n"
            " - ETA (mins): %d\n"
            " - Fare est: Rs %d\n",
            edges, total_dist, time_min, fare);

    fclose(f);
    printf("Saved TXT report: %s\n", fname);
}

void export_route_to_html(const char *fname, int path[], int len, char edgeLines[][30]) {
    FILE *f = fopen(fname, "w");
    if (!f) {
        printf("Failed to create %s\n", fname);
        return;
    }

    // Basic CSS for a clean look
    fprintf(f, "<!doctype html>\n<html><head><meta charset='utf-8'>\n");
    fprintf(f, "<title>Namma Metro Route Report</title>\n");
    fprintf(f,
        "<style>\n"
        "body{font-family:Segoe UI,Roboto,Arial,sans-serif;margin:24px;color:#222}\n"
        ".header{background:#f4f6fb;padding:14px;border-radius:8px;margin-bottom:18px}\n"
        ".h1{font-size:20px;margin:0}\n"
        ".badge{display:inline-block;padding:6px 10px;border-radius:12px;margin-right:6px;font-weight:700}\n"
        ".badge.purple{background:#f3e8ff;color:#5b2b8a}\n"
        ".badge.green{background:#e6f8f0;color:#0b7a42}\n"
        ".badge.pink{background:#fff0f6;color:#9b3b76}\n"
        ".section{margin-top:14px}\n"
        ".table{width:100%%;border-collapse:collapse;margin-top:8px}\n"
        ".table th,.table td{border:1px solid #e6e9ef;padding:8px;text-align:left}\n"
        ".small{color:#666;font-size:13px}\n"
        "</style>\n"
    );
    fprintf(f, "</head><body>\n");

    // Header
    time_t now = time(NULL);
    char timestr[80];
    strftime(timestr, sizeof timestr, "%c", localtime(&now));
    fprintf(f,
            "<div class='header'><div class='h1'>NAMMA METRO — ROUTE REPORT</div>"
            "<div class='small'>Generated: %s</div></div>\n",
            timestr);

    // Route
    fprintf(f, "<div><strong>Route:</strong> ");
    for (int i = 0; i < len; i++) {
        const char *nm = stations[path[i]].display_name[0]
                         ? stations[path[i]].display_name
                         : stations[path[i]].key_name;
        fprintf(f, "%s", nm);
        if (i < len - 1) fprintf(f, " &rarr; ");
    }
    fprintf(f, "</div>\n");

    // Segments
    fprintf(f,
            "<div class='section'><h3>Line segments</h3>"
            "<table class='table'><tr>"
            "<th>Line</th><th>Start</th><th>End</th><th>Stops</th>"
            "</tr>\n");

    int i2 = 0;
    while (i2 < len - 1) {
        char curr[30];
        strncpy(curr, edgeLines[i2], 29);
        curr[29] = '\0';

        int s = i2;
        int e = i2;
        while (e + 1 < len - 1 && strcmp(edgeLines[e + 1], curr) == 0) {
            e++;
        }

        const char *start = stations[path[s]].display_name[0]
                            ? stations[path[s]].display_name
                            : stations[path[s]].key_name;
        const char *end   = stations[path[e + 1]].display_name[0]
                            ? stations[path[e + 1]].display_name
                            : stations[path[e + 1]].key_name;

        const char *cls =
            strcmp(curr, "purple") == 0 ? "purple" :
            strcmp(curr, "green")  == 0 ? "green"  :
            strcmp(curr, "pink")   == 0 ? "pink"   : "";

        fprintf(f,
                "<tr><td><span class='badge %s'>%s</span></td>"
                "<td>%s</td><td>%s</td><td>%d</td></tr>\n",
                cls, curr, start, end, e - s + 1);

        i2 = e + 1;
    }
    fprintf(f, "</table></div>\n");

    // Per-edge breakdown
    fprintf(f,
            "<div class='section'><h3>Per-stop breakdown</h3>"
            "<table class='table'>"
            "<tr><th>From</th><th>To</th><th>Distance (km)</th>"
            "<th>Time (min)</th><th>Fare (slab)</th></tr>\n");

    int edges = len - 1;
    double total_dist = 0.0;
    int total_time = 0;

    for (int k = 0; k < edges; k++) {
        const char *a = stations[path[k]].display_name[0]
                        ? stations[path[k]].display_name
                        : stations[path[k]].key_name;
        const char *b = stations[path[k + 1]].display_name[0]
                        ? stations[path[k + 1]].display_name
                        : stations[path[k + 1]].key_name;

        double dist = AVG_KM_PER_EDGE;
        int tmin = TIME_PER_EDGE_MIN;
        int fare = fare_from_distance(dist);

        total_dist += dist;
        total_time += tmin;

        fprintf(f,
                "<tr><td>%s</td><td>%s</td><td>%.2f</td>"
                "<td>%d</td><td>Rs %d</td></tr>\n",
                a, b, dist, tmin, fare);
    }
    int interchanges = 0;
    for (int ii = 0; ii < len; ii++) {
        if (stations[path[ii]].line_count > 1)
            interchanges++;
    }

    int est_time = total_time + interchanges * INTERCHANGE_TIME_MIN;
    int est_fare = fare_from_distance(total_dist);

    fprintf(f, "</table></div>\n");

    // Summary
    fprintf(f,
            "<div class='section'><h3>Trip Summary</h3>\n<ul>\n"
            "<li>Stops traveled: %d</li>\n"
            "<li>Distance: %.2f km</li>\n"
            "<li>Estimated travel time: %d minutes (incl. %d mins interchange)</li>\n"
            "<li>Estimated fare: Rs %d</li>\n"
            "</ul>\n</div>\n",
            edges, total_dist, est_time, interchanges * INTERCHANGE_TIME_MIN, est_fare);

    fprintf(f,
            "<div style='margin-top:18px' class='small'>"
            "Generated by Namma Metro Route Finder"
            "</div>");

    fprintf(f, "</body></html>");
    fclose(f);
    printf("Saved HTML report: %s\n", fname);
}

// =============================================================
// CROSS-PLATFORM FILE OPEN (DEFAULT APP)
// =============================================================
void open_file_crossplatform(const char *filename) {
#ifdef _WIN32
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "start \"\" \"%s\"", filename);
    system(cmd);
#elif __APPLE__
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "open \"%s\"", filename);
    system(cmd);
#else
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "xdg-open \"%s\" >/dev/null 2>&1 &", filename);
    system(cmd);
#endif
}

// =============================================================
// SHOW ALL STATIONS
// =============================================================
void show_all_stations(int include_planned) {
    printf("\n%sStation list (total: %d)%s\n",
           CLR_BOLD, stationCount, CLR_RESET);

    for (int i = 0; i < stationCount; i++) {
        if (!include_planned && stations[i].planned)
            continue;

        const char *nm = stations[i].display_name[0]
                         ? stations[i].display_name
                         : stations[i].key_name;
        printf(" - %s", nm);

        if (stations[i].line_count > 0) {
            printf(" (");
            for (int j = 0; j < stations[i].line_count; j++) {
                printf("%s", stations[i].lines[j]);
                if (j + 1 < stations[i].line_count) printf(", ");
            }
            printf(")");
        }
        if (stations[i].planned)
            printf(" [planned]");

        printf("\n");
    }
}

// =============================================================
// WEBASSEMBLY ENTRY: get_route(from, to)
// =============================================================
EMSCRIPTEN_KEEPALIVE
const char* get_route(const char* from, const char* to) {
    static char buffer[4096];
    buffer[0] = '\0';

    int include_planned = 1;
    build_network(include_planned);

    if (stationCount == 0) {
        snprintf(buffer, sizeof buffer, "Error: station data not loaded.");
        return buffer;
    }

    if (!from || !to || !from[0] || !to[0]) {
        snprintf(buffer, sizeof buffer, "Please provide both source and destination.");
        return buffer;
    }

    char srckey[80], dstkey[80];
    strncpy(srckey, from, 79);
    srckey[79] = '\0';
    normalize_inplace(srckey);

    strncpy(dstkey, to, 79);
    dstkey[79] = '\0';
    normalize_inplace(dstkey);

    int src = -1, dest = -1;
    for (int i = 0; i < stationCount; i++) {
        if (!include_planned && stations[i].planned) continue;
        if (strcmp(srckey, stations[i].key_name) == 0) src = i;
        if (strcmp(dstkey, stations[i].key_name) == 0) dest = i;
    }

    if (src == -1 && dest == -1) {
        snprintf(buffer, sizeof buffer,
                 "Stations not found:\n - From: %s\n - To: %s\nCheck spellings or station availability.",
                 from, to);
        return buffer;
    } else if (src == -1) {
        snprintf(buffer, sizeof buffer,
                 "Start station not found: %s\nCheck spelling or choose another station.", from);
        return buffer;
    } else if (dest == -1) {
        snprintf(buffer, sizeof buffer,
                 "Destination station not found: %s\nCheck spelling or choose another station.", to);
        return buffer;
    }

    int parent[MAX];
    for (int i = 0; i < MAX; i++) parent[i] = -1;

    if (!bfs_simple(src, dest, parent)) {
        snprintf(buffer, sizeof buffer,
                 "No route found between '%s' and '%s'.", from, to);
        return buffer;
    }

    // reconstruct path
    int path[MAX];
    int len = 0;
    int cur = dest;
    while (cur != -1) {
        path[len++] = cur;
        cur = parent[cur];
    }
    for (int i = 0; i < len / 2; i++) {
        int t = path[i];
        path[i] = path[len - 1 - i];
        path[len - 1 - i] = t;
    }

    char edgeLines[ MAX ][30]; // safe upper bound
    build_edge_lines(path, len, edgeLines);

    int edges = len - 1;
    double total_dist = edges * AVG_KM_PER_EDGE;

    // count interchanges
    int interchanges = 0;
    for (int ii = 0; ii < len; ii++) {
        if (stations[path[ii]].line_count > 1)
            interchanges++;
    }
    int base_time = edges * TIME_PER_EDGE_MIN;
    int est_time = base_time + interchanges * INTERCHANGE_TIME_MIN;
    int est_fare = fare_from_distance(total_dist);

    const char *srcName = stations[path[0]].display_name[0]
                          ? stations[path[0]].display_name
                          : stations[path[0]].key_name;
    const char *dstName = stations[path[len - 1]].display_name[0]
                          ? stations[path[len - 1]].display_name
                          : stations[path[len - 1]].key_name;

    // build text
    size_t offset = 0;
    size_t remaining = sizeof buffer;

    #define APPEND_FMT(fmt, ...) do { \
        int n = snprintf(buffer + offset, remaining, fmt, __VA_ARGS__); \
        if (n < 0 || (size_t)n >= remaining) { \
            buffer[sizeof(buffer)-1] = '\0'; \
            return buffer; \
        } \
        offset += (size_t)n; \
        remaining -= (size_t)n; \
    } while (0)

    APPEND_FMT("Namma Metro — Route Summary\n\n", 0);
    // use dummy 0 because fmt has no %; to keep macro simple
    offset -= 1; remaining += 1; /* remove extra "0" printed above */
    buffer[offset] = '\0';

    // overwrite header properly without dummy:
    offset = 0; remaining = sizeof buffer;
    APPEND_FMT("Namma Metro — Route Summary\n\n", "");

    APPEND_FMT("From: %s\n", srcName);
    APPEND_FMT("To:   %s\n\n", dstName);

    APPEND_FMT("Route:\n", "");
    for (int i = 0; i < len; i++) {
        const char *nm = stations[path[i]].display_name[0]
                         ? stations[path[i]].display_name
                         : stations[path[i]].key_name;
        APPEND_FMT("%s", nm);
        if (i < len - 1) {
            APPEND_FMT(" -> ", "");
        }
    }
    APPEND_FMT("\n\n", "");

    // segments by line
    APPEND_FMT("Segments by line:\n", "");
    int i = 0;
    while (i < len - 1) {
        char curr[30];
        strncpy(curr, edgeLines[i], 29);
        curr[29] = '\0';

        int s = i;
        int e = i;
        while (e + 1 < len - 1 && strcmp(edgeLines[e + 1], curr) == 0) {
            e++;
        }

        const char *start = stations[path[s]].display_name[0]
                            ? stations[path[s]].display_name
                            : stations[path[s]].key_name;
        const char *end   = stations[path[e + 1]].display_name[0]
                            ? stations[path[e + 1]].display_name
                            : stations[path[e + 1]].key_name;

        APPEND_FMT(" - Line %s: %s -> %s (%d stops)\n",
                   curr, start, end, e - s + 1);

        i = e + 1;
    }

    APPEND_FMT("\nInterchanges:\n", "");
    if (interchanges == 0) {
        APPEND_FMT(" - None\n", "");
    } else {
        for (int ii = 0; ii < len; ii++) {
            if (stations[path[ii]].line_count > 1) {
                const char *nm = stations[path[ii]].display_name[0]
                                 ? stations[path[ii]].display_name
                                 : stations[path[ii]].key_name;
                APPEND_FMT(" - %s (", nm);
                for (int j = 0; j < stations[path[ii]].line_count; j++) {
                    APPEND_FMT("%s", stations[path[ii]].lines[j]);
                    if (j + 1 < stations[path[ii]].line_count) {
                        APPEND_FMT(", ", "");
                    }
                }
                APPEND_FMT(")\n", "");
            }
        }
    }

    APPEND_FMT("\nSummary:\n", "");
    APPEND_FMT(" - Total stops: %d\n", edges);
    APPEND_FMT(" - Distance   : %.2f km\n", total_dist);
    APPEND_FMT(" - Time       : %d min (incl. interchange buffer)\n", est_time);
    APPEND_FMT(" - Fare est.  : Rs %d\n", est_fare);

    #undef APPEND_FMT

    return buffer;
}

// =============================================================
// MAIN MENU / INTERACTIVE LOOP (CLI ONLY)
// =============================================================
#ifndef __EMSCRIPTEN__
int main(void) {
    // On Windows: switch console to UTF-8 so em-dash, arrows, emojis work.
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    int include_planned = 1; // currently all stations open; reserved for future
    build_network(include_planned);

    if (stationCount == 0) {
        printf("No stations loaded. Exiting.\n");
        return 0;
    }

    int last_path[MAX];
    int last_len = 0; // track if we have a last route

    while (1) {
        printf("\n%s%s Namma Metro — Professional Route Finder %s\n",
               CLR_BOLD CLR_CYAN,
               "========================================",
               CLR_RESET);
        printf(" 1) Find route\n");
        printf(" 2) Show stations\n");
        printf(" 3) Autocomplete suggestions\n");
        printf(" 4) Export last route / Generate report & Open\n");
        printf(" 5) Toggle planned stations (now: %s)\n",
               include_planned ? "ON" : "OFF");
        printf(" 6) Quit\n");
        printf("Choose (1-6): ");

        int choice = 0;
        if (scanf("%d%*c", &choice) != 1) {
            printf("Input error\n");
            return 0;
        }

        if (choice == 6) {
            printf("Goodbye!\n");
            break;
        } else if (choice == 2) {
            show_all_stations(include_planned);
            continue;
        } else if (choice == 5) {
            include_planned = !include_planned;
            build_network(include_planned);
            printf("Toggled include_planned -> %d\n", include_planned);
            continue;
        } else if (choice == 3) {
            char pref[80];
            printf("Type prefix (any case/punc):\n> ");
            if (!fgets(pref, sizeof pref, stdin)) continue;
            pref[strcspn(pref, "\n")] = '\0';
            pref[strcspn(pref, "\r")] = '\0';
            normalize_inplace(pref);
            autocomplete_print(pref);
            continue;
        } else if (choice == 4) {
            if (last_len == 0) {
                printf("No last route available. Run 'Find route' first.\n");
                continue;
            }
            // build edge lines for last path
            char edgeLines[last_len][30];
            build_edge_lines(last_path, last_len, edgeLines);

            // timestamped file names
            time_t t = time(NULL);
            struct tm *tm = localtime(&t);
            char txtfile[160];
            char htmlfile[160];

            snprintf(txtfile, sizeof txtfile,
                     "namma_route_%04d%02d%02d_%02d%02d.txt",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min);
            snprintf(htmlfile, sizeof htmlfile,
                     "namma_route_%04d%02d%02d_%02d%02d.html",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min);

            // export_route_to_txt(txtfile, last_path, last_len, edgeLines); txt export optional
            export_route_to_html(htmlfile, last_path, last_len, edgeLines);

#ifdef _WIN32
            // highlight the file in Explorer
            char explorerCmd[1024];
            snprintf(explorerCmd, sizeof explorerCmd,
                     "explorer /select,\"%s\"", htmlfile);
            system(explorerCmd);
#elif __APPLE__
            char openFolder[1024];
            snprintf(openFolder, sizeof openFolder,
                     "open -R \"%s\"", htmlfile);
            system(openFolder);
#else
            // on Linux, just open current directory
            char parentCmd[1024];
            snprintf(parentCmd, sizeof parentCmd,
                     "xdg-open . >/dev/null 2>&1 &");
            system(parentCmd);
#endif
            continue;
        } else if (choice == 1) {
            char srcraw[80], dstra[80];
            char srckey[80], dstkey[80];

            printf("Enter Start Station:\n> ");
            if (!fgets(srcraw, sizeof srcraw, stdin)) continue;
            srcraw[strcspn(srcraw, "\n")] = '\0';
            srcraw[strcspn(srcraw, "\r")] = '\0';

            strncpy(srckey, srcraw, 79);
            srckey[79] = '\0';
            normalize_inplace(srckey);

            printf("Enter Destination Station:\n> ");
            if (!fgets(dstra, sizeof dstra, stdin)) continue;
            dstra[strcspn(dstra, "\n")] = '\0';
            dstra[strcspn(dstra, "\r")] = '\0';

            strncpy(dstkey, dstra, 79);
            dstkey[79] = '\0';
            normalize_inplace(dstkey);

            int src = -1, dest = -1;
            for (int i = 0; i < stationCount; i++) {
                if (!include_planned && stations[i].planned) continue;
                if (strcmp(srckey, stations[i].key_name) == 0) src = i;
                if (strcmp(dstkey, stations[i].key_name) == 0) dest = i;
            }

            if (src == -1) {
                printf("Start station not found: '%s'\n", srcraw);
                autocomplete_print(srckey);
                continue;
            }
            if (dest == -1) {
                printf("Destination station not found: '%s'\n", dstra);
                autocomplete_print(dstkey);
                continue;
            }

            int parent[MAX];
            for (int i = 0; i < MAX; i++) parent[i] = -1;

            if (!bfs_simple(src, dest, parent)) {
                printf("No route found between '%s' and '%s'\n", srcraw, dstra);
                continue;
            }

            // reconstruct shortest path from BFS parent[]
            int path[MAX];
            int len = 0;
            int cur = dest;

            while (cur != -1) {
                path[len++] = cur;
                cur = parent[cur];
            }

            // reverse path to get src -> dest
            for (int i = 0; i < len / 2; i++) {
                int t = path[i];
                path[i] = path[len - 1 - i];
                path[len - 1 - i] = t;
            }

            // determine lines for each edge
            char edgeLines[len][30];
            build_edge_lines(path, len, edgeLines);

            // professional output
            print_final_output_professional(path, len, edgeLines);

            // ASCII preview
            print_ascii_map_preview(path, len);

            // alternate routes
            int alternates[MAX_ALTERNATES][MAX];
            int altlens[MAX_ALTERNATES];
            for (int i = 0; i < MAX_ALTERNATES; i++) altlens[i] = 0;

            int altc = find_alternates(path, len, alternates, altlens);
            if (altc > 0) {
                printf("%sAlternate suggestions:%s\n", CLR_BOLD, CLR_RESET);
                for (int a = 0; a < altc; a++) {
                    printf(" Alt %d) ", a + 1);
                    for (int p = 0; p < altlens[a]; p++) {
                        const char *nm = stations[alternates[a][p]].display_name[0]
                                         ? stations[alternates[a][p]].display_name
                                         : stations[alternates[a][p]].key_name;
                        printf("%s", nm);
                        if (p + 1 < altlens[a]) printf(" -> ");
                    }
                    printf("\n");
                }
            } else {
                printf("No alternate routes found.\n");
            }

            // store last path for export
            last_len = len;
            for (int i = 0; i < len; i++)
                last_path[i] = path[i];

        } else {
            printf("Unknown selection\n");
        }
    }

    return 0;
}
#endif // __EMSCRIPTEN__
