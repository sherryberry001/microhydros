# MicroHydros - gatewaytjanster
#
# En image for bade ingest och dashboard. De delar beroenden och ar bada nagra
# hundra rader Python, sa tva separata images hade bara gett tva saker att halla
# i takt utan nagon vinst.
#
# slim-basen och --no-cache-dir haller imagen runt 60 MB istallet for 130.

FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY schema.sql mqtt_compat.py ingest.py dashboard.py bridge.py ./

# Kor inte som root. Databasen ligger i en volym som ags av samma anvandare.
RUN useradd --create-home --uid 1000 microhydros \
    && mkdir -p /data \
    && chown -R microhydros:microhydros /data /app
USER microhydros

# Oavbuffrad utskrift, annars syns loggarna forst nar containern stoppas.
ENV PYTHONUNBUFFERED=1

CMD ["python3", "ingest.py", "--help"]
