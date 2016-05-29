#!/bin/bash

docker run -ti -e DISPLAY=unix$DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix -v $HOME:$HOME pvalsecc/qgis-desktop:latest