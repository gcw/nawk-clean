

oldawk=${oldawk-myawk}
awk=${awk-../a.out}

echo oldawk=$oldawk, awk=$awk

for i
do
	echo "$i:"
	$oldawk -f $i test.data >foo1 
	$awk -f $i test.data >foo2 
	if [ $? -ne 0 ]; then
		echo -n "$i:\tBAD\007 (non-zero exit status) ..."
	else
		if cmp -s foo1 foo2
			then true
		else echo -n "$i:\tBAD\007 ..."
		fi
	fi
	diff -b foo1 foo2 | sed -e 's/^/\t/' -e 10q
done

