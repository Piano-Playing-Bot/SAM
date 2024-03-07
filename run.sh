make main
if (make main); then
	cd ./bin
	if $1 -eq "r"; then
		rm -rf ./data
	fi
	./main
	cd ..
fi
