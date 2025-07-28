BEGIN {
    s = ""
    for (i = 0; i < 100000; i++) {
        s = s i " " (i * 2)
    }
    print "DONE"
    exit 1 # This will make the test fail initially
}