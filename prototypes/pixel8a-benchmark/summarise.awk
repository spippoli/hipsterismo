# Throwaway: summarise bench output by the Frame's long side.
#   awk -f summarise.awk results.txt...
# Prints per size: count, mean/min/max end-to-end median ms, last peak RSS,
# and the mean of each stage's median ms.
/^RESULT/ {
    split($3, d, "x"); key = d[1] > d[2] ? d[1] : d[2]
    split($9, m, "="); v = m[2] + 0
    n[key]++; e[key] += v
    if (v > mx[key]) mx[key] = v
    if (!(key in mn) || v < mn[key]) mn[key] = v
}
/^STAGES/ {
    for (i = 4; i <= NF; i++) { split($i, a, "="); s[key "," a[1]] += a[2]; names[i] = a[1] }
}
/^MEMORY/ { mem[key] = $3 }
END {
    for (k in n) {
        printf "%s n=%d mean=%.1f min=%.1f max=%.1f peakRSS=%dMB |", k, n[k], e[k] / n[k], mn[k], mx[k], int(mem[k] / 1024)
        for (i = 4; i <= 13; i++) printf " %s=%.2f", names[i], s[k "," names[i]] / n[k]
        print ""
    }
}
