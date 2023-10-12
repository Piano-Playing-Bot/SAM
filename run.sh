if ./build.sh; then
	cd ./bin
	if $1 -eq "r"; then
		rm -rf ./data
	fi
	./main
	cd ..
fi
