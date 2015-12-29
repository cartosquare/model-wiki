#ModelWiki

Model country relations based on wiki pages.

## Dependencies
* curl
* gdal >= 1.11.2
* gcc >= 4.9.2

## Build
```
gcc -std=c++11 -I/usr/include/gdal/ -lgdal -lcurl -lstdc++ -o modelwiki main.cpp
```

## Runing and Output
Use follow command to start running, this may take several hours.
```
$ cd <project-dir>
$ ./modelwiki
```

When completed, there will be a shapefile called relation.shp, which contains lines that link between countries.