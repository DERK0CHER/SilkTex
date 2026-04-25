if [[ -o interactive ]]; then
  [[ ! -r '/Users/beba/software/SilkTex/.opam-goblint/opam-init/complete.zsh' ]] || source '/Users/beba/software/SilkTex/.opam-goblint/opam-init/complete.zsh' > /dev/null 2> /dev/null

  [[ ! -r '/Users/beba/software/SilkTex/.opam-goblint/opam-init/env_hook.zsh' ]] || source '/Users/beba/software/SilkTex/.opam-goblint/opam-init/env_hook.zsh' > /dev/null 2> /dev/null
fi

[[ ! -r '/Users/beba/software/SilkTex/.opam-goblint/opam-init/variables.sh' ]] || source '/Users/beba/software/SilkTex/.opam-goblint/opam-init/variables.sh' > /dev/null 2> /dev/null
