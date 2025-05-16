docker run -e JUPYTER_TOKEN=token --rm -p 8888:8888 --mount type=bind,source="$(pwd)",target=/home/jovyan quay.io/jupyter/datascience-notebook

