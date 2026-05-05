FROM python:3.11-slim

WORKDIR /app

# Instalacja ffmpeg, curl i unzip pod yt-dlp / YouTube
RUN apt-get update && apt-get install -y ffmpeg curl unzip && rm -rf /var/lib/apt/lists/*

# Instalacja Deno jako runtime JS wymagany przez nowe flow YouTube w yt-dlp
RUN curl -fsSL https://deno.land/install.sh | sh -s -- -y
ENV PATH="/root/.deno/bin:${PATH}"

# Instalacja yt-dlp z domyslnymi zaleznosciami, w tym EJS scripts
RUN pip install --no-cache-dir "yt-dlp[default]"

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
