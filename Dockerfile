FROM python:3.11-slim

WORKDIR /app

# Instalacja ffmpeg, deno (JS runtime dla yt-dlp) i yt-dlp
RUN apt-get update && apt-get install -y ffmpeg curl unzip && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL https://deno.land/install.sh | DENO_INSTALL=/usr/local sh
RUN pip install --no-cache-dir --upgrade yt-dlp

# Instalacja zależności
COPY server/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Kopiowanie kodu
COPY server/ .

# Tworzenie katalogów
RUN mkdir -p /app/music /app/data /app/web

# Port
EXPOSE 8000

# Uruchomienie
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
