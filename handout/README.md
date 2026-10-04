# INF8601 — Script `handout.py`

Ce script permet de récupérer les énoncés de laboratoire et de soumettre votre
travail au serveur de correction.

## 1. Configuration du `.env`

Ouvrez le fichier `.env` à la racine du projet et remplissez les champs
suivants :

- `NOM_ETUDIANT_1` / `MATRICULE_ETUDIANT_1` : votre nom et matricule.
- `NOM_ETUDIANT_2` / `MATRICULE_ETUDIANT_2` : nom et matricule de votre
  coéquipier·ère (laissez vide si vous travaillez seul·e).
- `LABO_COURANT` : le numéro du laboratoire en cours (ex. `1`).
- `DOSSIER_LABO_COURANT` : le dossier local où sera extrait l'énoncé et à
  partir duquel vous soumettrez votre travail (ex. `./labo-1`).

Les champs suivants sont **déjà remplis** et ne doivent pas être modifiés :

- `ADRESSE_SERVEUR` : adresse du serveur de correction.
- `CLE_COURS` : clé secrète du cours, requise pour toutes les requêtes.

Le champ `JETON_API` doit rester vide : il sera rempli automatiquement par la
commande `teamup`.

## 2. Prérequis

Le script s'exécute avec [`uv`](https://docs.astral.sh/uv/), qui installe
automatiquement les dépendances au premier lancement.

## 3. Utilisation

Toutes les commandes se lancent avec :

```sh
uv run handout.py <commande> [options]
```

### Enregistrer votre équipe

À faire une seule fois, en premier :

```sh
uv run handout.py teamup
```

Enregistre votre équipe auprès du serveur et sauvegarde automatiquement le
`JETON_API` reçu dans votre `.env`.

### Récupérer l'énoncé d'un laboratoire

```sh
uv run handout.py fetch [--labo N] [-o DOSSIER]
```

Sans options, utilise `LABO_COURANT` et `DOSSIER_LABO_COURANT` du `.env`.

### Soumettre votre travail

```sh
uv run handout.py submit [DOSSIER] [--labo N] [--follow | --no-follow]
```

Sans options, soumet le contenu de `DOSSIER_LABO_COURANT`. Par défaut, le
script vous demande si vous voulez suivre l'avancement de la correction en
direct (`--follow` pour forcer le suivi, `--no-follow` pour soumettre sans
attendre).

### Suivre une soumission en cours

```sh
uv run handout.py status [--task ID_SOUMISSION]
```

Sans `--task`, suit la soumission active de votre équipe.

### Voir vos résultats

```sh
uv run handout.py result
```

Liste toutes vos soumissions avec leur statut et leur note.

### Annuler une soumission

```sh
uv run handout.py cancel [--task ID_SOUMISSION]
```
