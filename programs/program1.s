.text
main:
  li    $3, 0       # $3 = i = 0
  li    $5, 0       # $5 = sum = 0
L1:
  li    $4, 0       # $4 = j = 0
L2:
  add   $5, $5, $3  # sum += i
  addi  $4, $4, 1   # j++
  li    $6, 50
  bne   $4, $6, L2  # if(j != 50) goto L2
  addi  $3, $3, 1   # i++
  li    $6, 1000
  bne   $3, $6, L1  # if(i != 1000) goto L1
  b     exit
exit:
  li $2, 10
  syscall

.data
