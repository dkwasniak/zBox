FROM python:3.11-slim

WORKDIR /app

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
