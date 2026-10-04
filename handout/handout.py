"""INF8601 handout CLI: communication avec le serveur de correction

Usage:
    ./scripts/handout.py teamup
    ./scripts/handout.py fetch [--labo N] [-o DIR]
    ./scripts/handout.py submit [--labo N] [DIR] [--follow | --no-follow]
    ./scripts/handout.py status [--task ID]
    ./scripts/handout.py result
    ./scripts/handout.py cancel [--task ID]
"""

import argparse
import io
import json
import re
import sys
import tarfile
from pathlib import Path

import dotenv
import httpx


def _fail(message: str) -> None:
    print(f"Erreur : {message}", file=sys.stderr)
    raise SystemExit(1)


def _print_error(response: httpx.Response) -> None:
    try:
        body = response.json()
        detail = body.get("detail", body) if isinstance(body, dict) else body
    except ValueError:
        detail = response.text
    print(f"Erreur du serveur ({response.status_code}): {detail}", file=sys.stderr)


def _load_env(env_file: Path) -> dict[str, str]:
    if not env_file.exists():
        _fail(
            f"Fichier de configuration introuvable : {env_file}. "
            "Créez-le avant d'utiliser ce script (voir le docstring pour le format)."
        )
    return {k: v for k, v in dotenv.dotenv_values(env_file).items() if v is not None}


def _env_or(cli_value: str | None, key: str, env: dict[str, str]) -> str:
    if cli_value is not None:
        return cli_value
    value = env.get(key)
    if not value:
        _fail(f"{key} manquant : ajoutez-le au .env ou passez l'option correspondante.")
    return value


def _base_url(env: dict[str, str]) -> str:
    return _env_or(None, "ADRESSE_SERVEUR", env).rstrip("/")


def _course_key_headers(env: dict[str, str]) -> dict[str, str]:
    key = env.get("CLE_COURS")
    return {"X-Course-Key": key} if key else {}


def _auth_headers(env: dict[str, str]) -> dict[str, str]:
    token = _env_or(None, "JETON_API", env)
    headers = _course_key_headers(env)
    headers["Authorization"] = f"Bearer {token}"
    headers["X-Student1-Id"] = _env_or(None, "MATRICULE_ETUDIANT_1", env)
    sid2 = env.get("MATRICULE_ETUDIANT_2")
    if sid2:
        headers["X-Student2-Id"] = sid2
    return headers


def _tar_filter(tarinfo: tarfile.TarInfo) -> tarfile.TarInfo | None:
    parts = Path(tarinfo.name).parts
    if ".git" in parts or "build" in parts or "data" in parts:
        return None
    return tarinfo


def _tar_project(path: Path) -> bytes:
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tf:
        tf.add(path, arcname=".", filter=_tar_filter)
    return buf.getvalue()


def _prompt_yes_no(prompt: str, default: bool) -> bool:
    try:
        answer = input(prompt).strip().casefold()
    except EOFError:
        return default
    if not answer:
        return default
    return answer in ("o", "oui", "y", "yes")


_BUILD_PROGRESS_RE = re.compile(r"^\[\s*\d+%\]")


class _LogPrinter:
    """Prints streamed log chunks line by line. While consecutive lines look
    like cmake/make progress ("[ 42%] Building ..."), each new one overwrites
    the previous line in place instead of scrolling, so a multi-file build
    doesn't flood the terminal -- falls back to plain appending when stdout
    isn't a tty (piped/redirected output)."""

    def __init__(self) -> None:
        self._buf = ""
        self._overwriting = False
        self._tty = sys.stdout.isatty()

    def feed(self, chunk: str) -> None:
        self._buf += chunk
        while "\n" in self._buf:
            line, self._buf = self._buf.split("\n", 1)
            self._emit(line)

    def _emit(self, line: str) -> None:
        if self._tty and _BUILD_PROGRESS_RE.match(line):
            prefix = "\r\x1b[2K" if self._overwriting else ""
            sys.stdout.write(f"{prefix}{line}")
            self._overwriting = True
        else:
            if self._overwriting:
                sys.stdout.write("\n")
                self._overwriting = False
            sys.stdout.write(line + "\n")
        sys.stdout.flush()

    def finish(self) -> None:
        if self._buf:
            self._emit(self._buf)
            self._buf = ""
        if self._overwriting:
            sys.stdout.write("\n")
            self._overwriting = False
        sys.stdout.flush()


def _handle_sse_event(event: str, data: dict, printer: _LogPrinter) -> None:
    if event == "log":
        printer.feed(data["chunk"])
    elif event == "result":
        printer.finish()
        status, grade, summary = data["status"], data["grade"], data["summary"]
        print(f"\n--- Terminé : {status} ---")
        if grade is not None:
            print(f"Note : {grade:.2f}")
        if summary:
            print(summary)
    else:
        printer.finish()
        print(f"[{event}]")


def _follow_status(base_url: str, headers: dict[str, str], submission_id: str | None) -> None:
    params = {"submission": submission_id} if submission_id else {}
    printer = _LogPrinter()
    with httpx.stream(
        "GET", f"{base_url}/status", headers=headers, params=params, timeout=None
    ) as response:
        if response.status_code != 200:
            response.read()
            _print_error(response)
            raise SystemExit(1)

        event = None
        for line in response.iter_lines():
            if not line:
                continue
            if line.startswith("event:"):
                event = line[len("event:"):].strip()
            elif line.startswith("data:"):
                data = json.loads(line[len("data:"):].strip())
                _handle_sse_event(event, data, printer)
                if event == "result":
                    return


def cmd_teamup(args: argparse.Namespace, env_file: Path, env: dict[str, str]) -> None:
    sid1 = _env_or(None, "MATRICULE_ETUDIANT_1", env)
    name1 = _env_or(None, "NOM_ETUDIANT_1", env)
    sid2 = env.get("MATRICULE_ETUDIANT_2") or None
    name2 = env.get("NOM_ETUDIANT_2") or None

    body = {"student1_id": int(sid1), "student1_name": name1}
    if sid2:
        body["student2_id"] = int(sid2)
        body["student2_name"] = name2

    with httpx.Client(timeout=30) as client:
        response = client.post(
            f"{_base_url(env)}/teamup", json=body, headers=_course_key_headers(env)
        )

    if response.status_code >= 400:
        _print_error(response)
        raise SystemExit(1)

    token = response.json()["token"]
    dotenv.set_key(env_file, "JETON_API", token)
    print(f"Équipe enregistrée. Jeton sauvegardé dans {env_file}.")


def cmd_fetch(args: argparse.Namespace, env_file: Path, env: dict[str, str]) -> None:
    lab = _env_or(args.labo, "LABO_COURANT", env)
    out_dir = Path(_env_or(args.output, "DOSSIER_LABO_COURANT", env))
    headers = _auth_headers(env)

    with httpx.Client(timeout=60) as client:
        response = client.get(f"{_base_url(env)}/fetch/{lab}", headers=headers)

    if response.status_code >= 400:
        _print_error(response)
        raise SystemExit(1)

    out_dir.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(response.content), mode="r:gz") as tf:
        tf.extractall(path=out_dir, filter="data")
    print(f"labo-{lab} extrait dans {out_dir}")


def cmd_submit(args: argparse.Namespace, env_file: Path, env: dict[str, str]) -> None:
    lab = _env_or(args.labo, "LABO_COURANT", env)
    project_dir = Path(_env_or(args.dir, "DOSSIER_LABO_COURANT", env))
    if not project_dir.is_dir():
        _fail(f"Pas un dossier : {project_dir}")

    artifact = _tar_project(project_dir)
    headers = _auth_headers(env)

    with httpx.Client(timeout=60) as client:
        response = client.post(
            f"{_base_url(env)}/submit",
            headers=headers,
            data={"lab": lab},
            files={"artifact": ("project.tar.gz", artifact, "application/gzip")},
        )

    if response.status_code == 429:
        in_flight = response.json().get("detail", {}).get("in_flight", [])
        print("Trop de soumissions actives pour cette équipe :", file=sys.stderr)
        for submission_id in in_flight:
            print(f"  - {submission_id}", file=sys.stderr)
        raise SystemExit(1)

    if response.status_code >= 400:
        _print_error(response)
        raise SystemExit(1)

    body = response.json()
    submission_id = body["submission_id"]
    print(f"Soumission {submission_id} en file (position {body['queue_position']}).")

    follow = args.follow
    if follow is None:
        follow = _prompt_yes_no("Suivre le statut de la soumission ? [O/n] ", default=True)

    if follow:
        try:
            _follow_status(_base_url(env), headers, submission_id)
        except KeyboardInterrupt:
            print("\nArrêt du suivi (la soumission continue sur le serveur).")


def cmd_status(args: argparse.Namespace, env_file: Path, env: dict[str, str]) -> None:
    headers = _auth_headers(env)
    try:
        _follow_status(_base_url(env), headers, args.task)
    except KeyboardInterrupt:
        print("\nArrêt du suivi.")


def cmd_result(args: argparse.Namespace, env_file: Path, env: dict[str, str]) -> None:
    headers = _auth_headers(env)
    with httpx.Client(timeout=30) as client:
        response = client.get(f"{_base_url(env)}/result", headers=headers)

    if response.status_code >= 400:
        _print_error(response)
        raise SystemExit(1)

    submissions = response.json()
    if not submissions:
        print("Aucune soumission.")
        return

    for s in submissions:
        line = f"{s['submission_id']}  labo-{s['lab']}  {s['status']}"
        if s["grade"] is not None:
            line += f"  note: {s['grade']:.2f}"
        if s["summary"]:
            line += f"  ({s['summary']})"
        print(line)


def cmd_cancel(args: argparse.Namespace, env_file: Path, env: dict[str, str]) -> None:
    headers = _auth_headers(env)
    params = {"submission": args.task} if args.task else {}

    with httpx.Client(timeout=30) as client:
        response = client.post(f"{_base_url(env)}/cancel", headers=headers, params=params)

    if response.status_code >= 400:
        _print_error(response)
        raise SystemExit(1)

    print(f"Soumission {response.json()['submission_id']} annulée.")


def main() -> None:
    parser = argparse.ArgumentParser(description="INF8601 grading server client")
    parser.add_argument("--env-file", default=".env", help="Chemin du fichier .env (défaut : ./.env)")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("teamup").set_defaults(func=cmd_teamup)

    fetch = subparsers.add_parser("fetch")
    fetch.add_argument("--labo", default=None)
    fetch.add_argument("-o", "--output", default=None)
    fetch.set_defaults(func=cmd_fetch)

    submit = subparsers.add_parser("submit")
    submit.add_argument("dir", nargs="?", default=None)
    submit.add_argument("--labo", default=None)
    submit.add_argument("--follow", dest="follow", action="store_true", default=None)
    submit.add_argument("--no-follow", dest="follow", action="store_false")
    submit.set_defaults(func=cmd_submit)

    status = subparsers.add_parser("status")
    status.add_argument("--task", default=None)
    status.set_defaults(func=cmd_status)

    subparsers.add_parser("result").set_defaults(func=cmd_result)

    cancel = subparsers.add_parser("cancel")
    cancel.add_argument("--task", default=None)
    cancel.set_defaults(func=cmd_cancel)

    args = parser.parse_args()
    env_file = Path(args.env_file)
    env = _load_env(env_file)
    args.func(args, env_file, env)


if __name__ == "__main__":
    main()
