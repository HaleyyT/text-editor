FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc make python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN make

ENV PORT=8000
EXPOSE 8000

CMD ["python3", "demo/server.py"]
