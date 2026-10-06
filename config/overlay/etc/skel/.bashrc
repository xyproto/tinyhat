[[ $- != *i* ]] && return
PS1='\[\e[1;31m\]\u@\h\[\e[0m\]:\w\$ '
alias ls='ls --color=auto'
