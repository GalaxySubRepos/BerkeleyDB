# BerkeleyDB
 Unofficial repo of Oracle BerkeleyDB source

<https://www.oracle.com/database/technologies/related/berkeleydb-release-history.html>

## Build sql

Tested on macOS with Homebrew-ed `openssl`.

```bash
cd ./build_unix/
../dist/configure
make -j
ls -l .libs/*.{a,dylib,so}

cd ..

mkdir ./lang/sql/build
cd ./lang/sql/build
../sqlite/configure

patch <../../../galaxy/make.patch
#patch -p0 -d ../sqlite <../../../galaxy/sqliteTool.patch
cp -r ../../../galaxy/tool ../sqlite/
```

## Mirroring Script

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
