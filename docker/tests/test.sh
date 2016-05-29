#!/bin/bash
set -e

function checkUrl {
  local url=$1
  local content=`curl -s $url`
  if echo "$content" | grep ServiceExceptionReport
  then
    echo "Error with $url:"
    echo "$content"
    exit 1
  else
    echo "Success with $url"
  fi
}

docker stop server 2>/dev/null || true
docker rm server 2>/dev/null || true

#docker build -t qgis-server ..
docker run -d -p 8380:80 --volume=$PWD:/project --name server pvalsecc/qgis-server:latest

BASE_URL=http://localhost:8380/

checkUrl "$BASE_URL?SERVICE=WMS&REQUEST=GetCapabilities"
checkUrl "$BASE_URL?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetMap&BBOX=-0.69003700000000001,-1.54244000000000003,1.30996000000000001,1.03689999999999993&CRS=EPSG:4326&WIDTH=667&HEIGHT=518&LAYERS=Test&STYLES=&FORMAT=image/jpeg&DPI=96&MAP_RESOLUTION=96&FORMAT_OPTIONS=dpi:96"

docker stop server
