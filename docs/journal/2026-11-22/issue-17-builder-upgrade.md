# Issue 17 Windows builder update

The v0.1.36 ZIP build completed all six game steps and deployed Order of the Flame. The warning appeared in the
optional HD texture step, after deployment. The transcript recorded Python 3.14.10. The screenshot showed that
the builder could only download wheels for Python 3.12. The attached transcript omitted the Python error and reported that
replacement files were available because the play folder already contained some. [Issue report and build log](https://github.com/ChristopherVR/Drakan2PC/issues/17#issuecomment-7069052189)

The previous wheel selection reproduced the same error with a clean Python 3.14.10. The new selection downloaded
the pinned wheels and processed a test image on Windows with clean x64 CPython 3.11.9, 3.12.10, 3.13.5, and 3.14.10.
A Windows builder run with an existing replacement file and a simulated HD error recorded the cause in
`build-log.txt`, reported that generation did not finish, and still completed successfully.
