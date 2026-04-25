if [ -t 0 ]; then
  test -r '/Users/beba/software/SilkTex/.opam-goblint/opam-init/complete.sh' && . '/Users/beba/software/SilkTex/.opam-goblint/opam-init/complete.sh' > /dev/null 2> /dev/null || true

  test -r '/Users/beba/software/SilkTex/.opam-goblint/opam-init/env_hook.sh' && . '/Users/beba/software/SilkTex/.opam-goblint/opam-init/env_hook.sh' > /dev/null 2> /dev/null || true
fi

test -r '/Users/beba/software/SilkTex/.opam-goblint/opam-init/variables.sh' && . '/Users/beba/software/SilkTex/.opam-goblint/opam-init/variables.sh' > /dev/null 2> /dev/null || true
