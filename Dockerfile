FROM python:3.12-slim-bookworm AS base
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 \
    CC=gcc CFLAGS="-fstack-protector-strong -D_FORTIFY_SOURCE=3 -fPIE -Wl,-z,relro,-z,now" \
    FKEN_BUILD_DIR=/app/.build FKEN_WEB_HOST=0.0.0.0 PORT=8080 FKEN_ALLOWED_HOSTS=*
WORKDIR /app

FROM base AS builder
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*
COPY compiler.py language_service.py web_server.py fken_cpl.c typedef.c util.c lex.c parser.c analyze.c generate_cpp.c runtime.hpp ./
RUN python3 -c "from compiler import build_compiler; build_compiler()"

FROM base AS runtime
COPY --from=builder /app /app
COPY web/ ./web/
COPY example_vectors.fken example_functions.fken example_sum.fken example_bigint.fken ./
USER 10001:10001
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/api/health', timeout=2)"
CMD ["python3", "web_server.py"]
