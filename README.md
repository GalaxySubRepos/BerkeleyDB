# BerkeleyDB
 Unofficial repo of Oracle BerkeleyDB source

<https://www.oracle.com/database/technologies/related/berkeleydb-release-history.html>

## Script

```bash
rm -fr *
git checkout README.md

tar -xzvf ../t/db-6.1.38.tar.gz
mv db-6.1.38/* .
rmidr db-6.1.38
git add -A
git commit .
git tag db-6.1.38

# git tag -l
git push --tags
```

